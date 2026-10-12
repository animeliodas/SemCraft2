package dev.semcraft2;

import it.unimi.dsi.fastutil.longs.Long2ByteOpenHashMap;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.TargetBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.phys.AABB;

/**
 * Serious Sam's moving brushes (doors, lifts, platforms, rotating machinery) in Minecraft. Sam writes their triangles
 * in each brush's own coordinates (level_<hash>.mov) and sends a brush's placement whenever it moves; each placement is
 * voxelized here into invisible shell blocks (the top ones as tall as the surface inside them), the cells it left are
 * given back to the static shell or the air, and mobs and items standing on top ride along.
 */
public final class Movers {
	/** Blocks changed per server tick at most (a giant moving structure must not stall the server). */
	private static final int BUDGET = 20000;
	/** Only movers this close to the player (blocks) are kept up to date. */
	private static final double RANGE = 160.0;
	/** Surface sampling step on the triangles (blocks). */
	private static final double STEP = 0.3;
	private static final int FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE | Block.UPDATE_SUPPRESS_DROPS;

	/** One mover: its triangles in its own coordinates, 9 floats each. */
	private record Mover(float[] tris, float radius) {
	}

	private static volatile Mover[] pendingSet;
	/** Link thread -> server thread: the newest placement per mover, [x, y, z, m11..m33] in Sam coordinates. */
	private static final Map<Integer, double[]> latest = new ConcurrentHashMap<>();

	// server thread only
	private static Mover[] movers = new Mover[0];
	/** Cells each mover fills now, with the shell height there. */
	private static final Map<Integer, Long2ByteOpenHashMap> cells = new HashMap<>();
	/** Where each mover was last applied (Minecraft coordinates), for carrying what stands on it. */
	private static final Map<Integer, double[]> applied = new HashMap<>();
	private static long updates, blocksSet, triggers, pulses;
	/** Game time each mover last moved, and was last triggered from here. */
	private static final Map<Integer, Long> lastMoved = new HashMap<>();
	private static final Map<Integer, Long> lastTriggered = new HashMap<>();
	/** Redstone reaches this far (blocks) from a mover's cells, both ways. */
	private static final int REDSTONE_REACH = 6;
	/** Dev: draw mover cells as slime blocks to see them against Sam's brushes. */
	private static volatile boolean debugVisible;

	private Movers() {
	}

	/** Loader thread (with the level's offset already set): read level_<hash>.mov next to the .tri, if Sam wrote one. */
	static void load(final Path file) {
		latest.clear();
		if (!Files.exists(file)) {
			pendingSet = new Mover[0];
			return;
		}

		try {
			ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(file)).order(ByteOrder.LITTLE_ENDIAN);
			if (b.remaining() < 16 || b.getInt(0) != 0x314D4353 || b.getInt(4) != 1) {
				throw new IOException("not a SemCraft movers file");
			}

			int n = b.getInt(8);
			b.position(16);
			Mover[] set = new Mover[n];
			int tris = 0;
			for (int i = 0; i < n; i++) {
				int count = b.getInt();
				float[] t = new float[count * 9];
				float r = 0.0F;
				for (int k = 0; k < t.length; k++) {
					t[k] = b.getFloat();
				}

				for (int k = 0; k < t.length; k += 3) {
					r = Math.max(r, (float) Math.sqrt(t[k] * t[k] + t[k + 1] * t[k + 1] + t[k + 2] * t[k + 2]));
				}

				set[i] = new Mover(t, r);
				tris += count;
			}

			pendingSet = set;
			SemCraft.LOG.info("movers: {} moving brushes, {} triangles", n, tris);
		} catch (IOException | RuntimeException e) {
			SemCraft.LOG.warn("couldn't read movers {}", file, e);
			pendingSet = new Mover[0];
		}
	}

	/** Link thread: [[index, x, y, z, m11, m12, m13, m21, m22, m23, m31, m32, m33], ...] (Sam coordinates). */
	public static void update(final double[][] list) {
		for (double[] a : list) {
			if (a.length >= 13) {
				latest.put((int) a[0], java.util.Arrays.copyOfRange(a, 1, 13));
			}
		}
	}

	public static void debug(final boolean visible) {
		debugVisible = visible;
	}

	/** Server tick. */
	static void tick(final ServerLevel level, final ServerPlayer player) {
		Mover[] next = pendingSet;
		if (next != null) {
			pendingSet = null;
			clearAll(level);
			movers = next;
		}

		if (movers.length == 0 || latest.isEmpty() || player == null) {
			return;
		}

		int budget = BUDGET;
		for (Map.Entry<Integer, double[]> e : latest.entrySet()) {
			int i = e.getKey();
			if (i < 0 || i >= movers.length || budget <= 0) {
				continue;
			}

			double[] p = e.getValue();
			double x = Mapping.mcX(p[0]), y = Mapping.mcY(p[1]), z = Mapping.mcZ(p[2]);
			if (player.distanceToSqr(x, y, z) > (RANGE + movers[i].radius()) * (RANGE + movers[i].radius())) {
				continue; // later, when the player is near (the placement stays waiting)
			}

			latest.remove(i, p);
			budget -= apply(level, i, p);
		}
	}

	/** Voxelize mover i at placement p, replace its old cells; returns the blocks changed. */
	private static int apply(final ServerLevel level, final int i, final double[] p) {
		double[] at = {Mapping.mcX(p[0]), Mapping.mcY(p[1]), Mapping.mcZ(p[2])};
		Long2ByteOpenHashMap now = voxelize(movers[i], at, p);
		Long2ByteOpenHashMap old = cells.getOrDefault(i, new Long2ByteOpenHashMap());

		// what stands on the mover moves with it; a mover that starts moving pulses the redstone targets next to it
		double[] before = applied.get(i);
		if (before != null && !old.isEmpty()) {
			double dx = at[0] - before[0], dy = at[1] - before[1], dz = at[2] - before[2];
			carry(level, old, dx, dy, dz);
			boolean moved = dx * dx + dy * dy + dz * dz > 0.05 * 0.05 || !sameCells(old, now);
			if (moved) {
				long t = level.getGameTime();
				Long last = lastMoved.put(i, t);
				if (last == null || t - last > 20) {
					pulseTargets(level, now);
				}
			}
		}

		int changed = 0;
		WorldBridge.quietGround(true);
		try {
			for (Long2ByteOpenHashMap.Entry c : old.long2ByteEntrySet()) {
				if (!now.containsKey(c.getLongKey())) {
					changed += release(level, BlockPos.of(c.getLongKey()));
				}
			}

			for (Long2ByteOpenHashMap.Entry c : now.long2ByteEntrySet()) {
				if (old.get(c.getLongKey()) != c.getByteValue() || !old.containsKey(c.getLongKey())) {
					changed += occupy(level, BlockPos.of(c.getLongKey()), c.getByteValue());
				}
			}
		} finally {
			WorldBridge.quietGround(false);
		}

		cells.put(i, now);
		applied.put(i, at);
		updates++;
		blocksSet += changed;
		return Math.max(1, changed);
	}

	/** Sample the mover's surfaces at placement p: cell -> shell height (16, or the surface height in a top cell). */
	private static Long2ByteOpenHashMap voxelize(final Mover m, final double[] at, final double[] p) {
		Long2ByteOpenHashMap out = new Long2ByteOpenHashMap();
		Long2ByteOpenHashMap top = new Long2ByteOpenHashMap(); // highest surface sample in each cell, in 1/16
		float[] t = m.tris();
		double[] w = new double[9];
		for (int k = 0; k < t.length; k += 9) {
			for (int v = 0; v < 3; v++) {
				double lx = t[k + v * 3], ly = t[k + v * 3 + 1], lz = t[k + v * 3 + 2];
				w[v * 3] = at[0] + p[3] * lx + p[4] * ly + p[5] * lz;
				w[v * 3 + 1] = at[1] + p[6] * lx + p[7] * ly + p[8] * lz;
				w[v * 3 + 2] = at[2] + p[9] * lx + p[10] * ly + p[11] * lz;
			}

			double e1 = Math.sqrt(sq(w[3] - w[0]) + sq(w[4] - w[1]) + sq(w[5] - w[2]));
			double e2 = Math.sqrt(sq(w[6] - w[0]) + sq(w[7] - w[1]) + sq(w[8] - w[2]));
			int n = (int) Math.ceil(Math.max(e1, e2) / STEP) + 1;
			for (int a = 0; a <= n; a++) {
				for (int b = 0; a + b <= n; b++) {
					double u = (double) a / n, v = (double) b / n;
					double x = w[0] + (w[3] - w[0]) * u + (w[6] - w[0]) * v;
					double y = w[1] + (w[4] - w[1]) * u + (w[7] - w[1]) * v;
					double z = w[2] + (w[5] - w[2]) * u + (w[8] - w[2]) * v;
					int cy = (int) Math.floor(y);
					long key = BlockPos.asLong((int) Math.floor(x), cy, (int) Math.floor(z));
					byte h = (byte) Math.clamp((long) Math.ceil((y - cy) * 16.0), 1, 16);
					if (h > top.getOrDefault(key, (byte) 0)) {
						top.put(key, h);
					}
				}
			}
		}

		// cells with the mover above them are full; top cells are as tall as the surface in them
		for (Long2ByteOpenHashMap.Entry c : top.long2ByteEntrySet()) {
			BlockPos pos = BlockPos.of(c.getLongKey());
			out.put(c.getLongKey(), top.containsKey(pos.above().asLong()) ? (byte) 16 : c.getByteValue());
		}

		return out;
	}

	private static double sq(final double v) {
		return v * v;
	}

	private static int occupy(final ServerLevel level, final BlockPos pos, final byte height) {
		if (!level.isLoaded(pos) || !level.isInWorldBounds(pos)) {
			return 0;
		}

		BlockState st = level.getBlockState(pos);
		if (!st.isAir() && !ShellBlock.is(st) && !isDebug(st)) {
			return 0; // a block the player put there stays
		}

		// where the static shell is taller (the mover overlaps a wall), the shell wins
		int h = Math.max(height, LevelShell.shellHeight(pos));
		BlockState want = debugVisible ? Blocks.SLIME_BLOCK.defaultBlockState() : ShellBlock.withHeight(h);
		if (st != want) {
			level.setBlock(pos, want, FLAGS);
			return 1;
		}

		return 0;
	}

	private static int release(final ServerLevel level, final BlockPos pos) {
		if (!level.isLoaded(pos)) {
			return 0;
		}

		BlockState st = level.getBlockState(pos);
		if (!ShellBlock.is(st) && !isDebug(st)) {
			return 0;
		}

		int h = LevelShell.shellHeight(pos);
		level.setBlock(pos, h > 0 ? ShellBlock.withHeight(h) : Blocks.AIR.defaultBlockState(), FLAGS);
		return 1;
	}

	private static boolean isDebug(final BlockState st) {
		return st.getBlock() == Blocks.SLIME_BLOCK;
	}

	/** Mobs and items standing on the mover's old cells move by (dx, dy, dz). */
	private static void carry(final ServerLevel level, final Long2ByteOpenHashMap old, final double dx, final double dy, final double dz) {
		if (dx * dx + dy * dy + dz * dz < 1.0E-6) {
			return;
		}

		AABB box = null;
		for (long key : old.keySet()) {
			BlockPos c = BlockPos.of(key);
			AABB a = new AABB(c.getX(), c.getY(), c.getZ(), c.getX() + 1, c.getY() + 2.5, c.getZ() + 1);
			box = box == null ? a : box.minmax(a);
		}

		if (box == null) {
			return;
		}

		List<Entity> riders = level.getEntities((Entity) null, box, e -> !(e instanceof ServerPlayer) && !SamMonsters.isProxy(e) && !e.isPassenger());
		for (Entity e : riders) {
			long below = BlockPos.containing(e.getX(), e.getBoundingBox().minY - 0.1, e.getZ()).asLong();
			if (old.containsKey(below)) {
				e.setPos(e.getX() + dx, e.getY() + dy, e.getZ() + dz);
			}
		}
	}

	private static boolean sameCells(final Long2ByteOpenHashMap a, final Long2ByteOpenHashMap b) {
		return a.size() == b.size() && a.keySet().containsAll(b.keySet());
	}

	/** Sam's door or lift started moving: Minecraft target blocks near it give a redstone pulse (one second). */
	private static void pulseTargets(final ServerLevel level, final Long2ByteOpenHashMap cellsNow) {
		int[] lo = {Integer.MAX_VALUE, Integer.MAX_VALUE, Integer.MAX_VALUE};
		int[] hi = {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
		for (long key : cellsNow.keySet()) {
			BlockPos p = BlockPos.of(key);
			lo[0] = Math.min(lo[0], p.getX()); lo[1] = Math.min(lo[1], p.getY()); lo[2] = Math.min(lo[2], p.getZ());
			hi[0] = Math.max(hi[0], p.getX()); hi[1] = Math.max(hi[1], p.getY()); hi[2] = Math.max(hi[2], p.getZ());
		}

		if (lo[0] > hi[0] || (long) (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]) > 200_000L) {
			return; // nothing there, or a giant structure: not a door
		}

		for (BlockPos p : BlockPos.betweenClosed(lo[0] - REDSTONE_REACH, lo[1] - REDSTONE_REACH, lo[2] - REDSTONE_REACH,
			hi[0] + REDSTONE_REACH, hi[1] + REDSTONE_REACH, hi[2] + REDSTONE_REACH)) {
			BlockState st = level.getBlockState(p);
			if (st.getBlock() instanceof TargetBlock && level.isLoaded(p)) {
				level.setBlock(p, st.setValue(BlockStateProperties.POWER, 15), Block.UPDATE_ALL);
				level.scheduleTick(p.immutable(), st.getBlock(), 20); // the target's own tick turns it off
				pulses++;
			}
		}
	}

	/**
	 * A redstone lamp lit up (server thread, from Level.setBlock): the nearest Sam mover within reach of it is triggered
	 * in Sam, as if its switch was used, at most once a second and a half.
	 */
	public static void onLampLit(final ServerLevel level, final BlockPos pos) {
		int best = -1;
		int bestD = Integer.MAX_VALUE;
		for (Map.Entry<Integer, Long2ByteOpenHashMap> e : cells.entrySet()) {
			for (long key : e.getValue().keySet()) {
				BlockPos c = BlockPos.of(key);
				int d = Math.max(Math.abs(c.getX() - pos.getX()), Math.max(Math.abs(c.getY() - pos.getY()), Math.abs(c.getZ() - pos.getZ())));
				if (d < bestD) {
					bestD = d;
					best = e.getKey();
				}
			}
		}

		if (best < 0 || bestD > REDSTONE_REACH) {
			return;
		}

		long t = level.getGameTime();
		Long last = lastTriggered.get(best);
		if (last != null && t - last < 30) {
			return;
		}

		lastTriggered.put(best, t);
		triggers++;
		SemCraft.LOG.info("redstone at {} triggers Sam's mover {} ({} blocks away)", pos, best, bestD);
		SemCraft.events.accept("{\"t\":\"trigger\",\"mover\":" + best + "}");
	}

	/** Every mover's cells back to the shell or the air (a new level, the world closing). */
	static void clearAll(final ServerLevel level) {
		if (level != null) {
			WorldBridge.quietGround(true);
			try {
				for (Long2ByteOpenHashMap m : cells.values()) {
					for (long key : m.keySet()) {
						release(level, BlockPos.of(key));
					}
				}
			} finally {
				WorldBridge.quietGround(false);
			}
		}

		cells.clear();
		applied.clear();
		lastMoved.clear();
		lastTriggered.clear();
	}

	/**
	 * Dev/test: mover i's cells as {"t":"moverinfo","i":i,"cells":n,"min":[x,y,z],"max":[x,y,z]} (Sam coordinates of
	 * the cell box corners), worked out on the server thread.
	 */
	public static String info(final int i) {
		net.minecraft.server.MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return "{\"t\":\"moverinfo\",\"i\":" + i + ",\"cells\":-1}";
		}

		try {
			return s.submit(() -> {
				Long2ByteOpenHashMap m = cells.get(i);
				if (m == null || m.isEmpty()) {
					return "{\"t\":\"moverinfo\",\"i\":" + i + ",\"cells\":0}";
				}

				int[] lo = {Integer.MAX_VALUE, Integer.MAX_VALUE, Integer.MAX_VALUE};
				int[] hi = {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
				for (long key : m.keySet()) {
					BlockPos p = BlockPos.of(key);
					int[] c = {p.getX(), p.getY(), p.getZ()};
					for (int a = 0; a < 3; a++) {
						lo[a] = Math.min(lo[a], c[a]);
						hi[a] = Math.max(hi[a], c[a] + 1);
					}
				}

				return String.format(Locale.ROOT, "{\"t\":\"moverinfo\",\"i\":%d,\"cells\":%d,\"min\":[%.1f,%.1f,%.1f],\"max\":[%.1f,%.1f,%.1f]}",
					i, m.size(), Mapping.samX(lo[0]), Mapping.samY(lo[1]), Mapping.samZ(lo[2]), Mapping.samX(hi[0]), Mapping.samY(hi[1]), Mapping.samZ(hi[2]));
			}).get(2, java.util.concurrent.TimeUnit.SECONDS);
		} catch (Exception e) {
			return "{\"t\":\"moverinfo\",\"i\":" + i + ",\"cells\":-2}";
		}
	}

	/** For the dev status. */
	public static String status() {
		int n = 0;
		for (Long2ByteOpenHashMap m : cells.values()) {
			n += m.size();
		}

		return String.format(Locale.ROOT, "{\"movers\":%d,\"placed\":%d,\"cells\":%d,\"updates\":%d,\"blocks\":%d,\"waiting\":%d,\"triggers\":%d,\"pulses\":%d}",
			movers.length, cells.size(), n, updates, blocksSet, latest.size(), triggers, pulses);
	}
}
