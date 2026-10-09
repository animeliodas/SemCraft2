package dev.semcraft2;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import it.unimi.dsi.fastutil.longs.Long2ByteMap;
import it.unimi.dsi.fastutil.longs.Long2ByteOpenHashMap;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.storage.LevelResource;

/**
 * The current Sam level as a shell of {@link ShellBlock}s.
 *
 * <p>Sam announces a level (its triangles in a .tri file). Each level gets a region of its own in the Minecraft world
 * (so blocks built in one level stay there). Chunks around the player are voxelised on worker threads as the player
 * gets near them (Sam's levels are kilometres wide) and placed within a block budget per server tick.
 */
public final class LevelShell {
	/** Levels are this many blocks apart along +x. */
	private static final int REGION_SPACING = 32768;
	/** Shell chunks are built within this many chunks of the player. */
	private static final int RADIUS_CHUNKS = 8;
	/** Chunks being voxelised at once. */
	private static final int MAX_JOBS = 16;
	/** At most this many shell blocks are set per server tick. */
	private static final int BUDGET = 30000;
	/** The level counts as ready once the chunks this close to the player are in. */
	private static final int READY_RADIUS = 3;
	private static final int FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE | Block.UPDATE_SUPPRESS_DROPS;
	private static final Gson GSON = new Gson();
	private static final ExecutorService LOADER = Executors.newSingleThreadExecutor(r -> daemon(r, "semcraft2-level"));
	private static final ExecutorService WORKERS = Executors.newFixedThreadPool(2, r -> daemon(r, "semcraft2-voxelizer"));

	private static Thread daemon(final Runnable r, final String name) {
		Thread t = new Thread(r, name);
		t.setDaemon(true);
		return t;
	}

	/** A level ready to stream. */
	private record Shell(String hash, String name, Voxelizer.Placed placed, int generation) {
	}

	/** A voxelised chunk on its way to the server thread. */
	private record Done(int generation, long chunk, Long2ByteOpenHashMap voxels) {
	}

	private static final AtomicInteger generations = new AtomicInteger();
	private static volatile Shell pending;
	private static volatile String loadingHash;
	private static final ConcurrentLinkedQueue<Done> done = new ConcurrentLinkedQueue<>();
	// server thread only
	private static Shell current;
	private static final LongOpenHashSet queued = new LongOpenHashSet();
	private static final LongOpenHashSet placed = new LongOpenHashSet();
	/** Every voxel of the chunks built so far (to put back what the player breaks). */
	private static final Long2ByteOpenHashMap voxels = new Long2ByteOpenHashMap();
	private static int jobs;
	private static long blocksPlaced;
	private static boolean announced;
	private static long loadStartNanos;

	private LevelShell() {
	}

	/** The last level Sam announced (kept for when the world opens after the announcement). */
	private static volatile String[] wanted;

	/** Link thread: Sam loaded a level. */
	public static void load(final String name, final String file, final String hash) {
		wanted = new String[] {name, file, hash};
		MinecraftServer server = WorldBridge.server();
		if (server == null) {
			SemCraft.LOG.info("level {} announced before the world is open: building it once the world is up", name);
			return;
		}

		Shell cur = current;
		if (cur != null && cur.hash().equals(hash)) {
			// Sam reconnected (or loaded a save of the same level): it needs the solid blocks again
			server.execute(() -> WorldBridge.syncBlocks(32));
			return;
		}

		if (hash.equals(loadingHash)) {
			return;
		}

		loadingHash = hash;
		int region = regionOf(server, name);
		LOADER.execute(() -> {
			try {
				long t0 = System.nanoTime();
				Voxelizer.Level level = Voxelizer.read(Path.of(file));
				double ox = (double) REGION_SPACING * (region + 1);
				double oy = 0.0;
				float minY = level.bboxMin()[1], maxY = level.bboxMax()[1];
				if (minY < -1000.0F || maxY > 1000.0F) {
					// too tall for the world (Sam levels hang backgrounds kilometres up): centre on where most of the
					// level's geometry is (the median vertex height), which is where it's played
					oy = -Math.floor(medianY(level));
				}

				Voxelizer.Placed placedLevel = new Voxelizer.Placed(level, ox, oy, 0.0);
				Mapping.setOffset(ox, oy, 0.0);
				pending = new Shell(hash, name, placedLevel, generations.incrementAndGet());
				SemCraft.LOG.info(String.format(Locale.ROOT, "level %s: %d triangles (%d solid), bounds y %.0f..%.0f, region %d, offset (%.0f, %.0f, 0), indexed in %.0f ms",
					name, level.count(), placedLevel.solidTriangles(), minY, maxY, region, ox, oy, (System.nanoTime() - t0) / 1.0E6));
			} catch (IOException | RuntimeException e) {
				SemCraft.LOG.error("couldn't load the level {}", name, e);
				loadingHash = null;
			}
		});
	}

	/** Median height of the level's vertices (Sam coordinates). */
	private static double medianY(final Voxelizer.Level level) {
		float[] t = level.tris();
		float[] ys = new float[level.count() * 3];
		for (int i = 0; i < level.count(); i++) {
			ys[i * 3] = t[i * 13 + 1];
			ys[i * 3 + 1] = t[i * 13 + 4];
			ys[i * 3 + 2] = t[i * 13 + 7];
		}

		java.util.Arrays.sort(ys);
		return ys.length == 0 ? 0.0 : ys[ys.length / 2];
	}

	/** The level region of a Sam level, remembered in the world folder so built blocks stay with their level. */
	private static synchronized int regionOf(final MinecraftServer server, final String name) {
		Path file = server.getWorldPath(LevelResource.ROOT).resolve("semcraft2_levels.json");
		JsonObject map = new JsonObject();
		try {
			if (Files.exists(file)) {
				map = GSON.fromJson(Files.readString(file, StandardCharsets.UTF_8), JsonObject.class);
			}
		} catch (IOException | RuntimeException e) {
			SemCraft.LOG.warn("couldn't read {}", file, e);
		}

		String key = name.toLowerCase(Locale.ROOT).replace('/', '\\');
		if (map.has(key)) {
			return map.get(key).getAsInt();
		}

		int region = map.size();
		map.addProperty(key, region);
		try {
			Files.writeString(file, GSON.toJson(map), StandardCharsets.UTF_8);
		} catch (IOException e) {
			SemCraft.LOG.warn("couldn't write {}", file, e);
		}

		return region;
	}

	/** Height (1..16) of the shell voxel at pos, or 0 if it isn't part of the current level's shell. */
	public static int shellHeight(final BlockPos pos) {
		return current == null ? 0 : voxels.getOrDefault(pos.asLong(), (byte) 0);
	}

	/** Server tick: swap in a new level, queue chunks near the player, place finished ones. */
	static void tick(final MinecraftServer server) {
		Shell next = pending;
		if (next != null) {
			pending = null;
			current = next;
			queued.clear();
			placed.clear();
			voxels.clear();
			done.clear();
			jobs = 0;
			blocksPlaced = 0;
			announced = false;
			loadingHash = null;
			loadStartNanos = System.nanoTime();
		}

		Shell s = current;
		if (s == null || server.getPlayerList().getPlayers().isEmpty()) {
			return;
		}

		ServerPlayer player = server.getPlayerList().getPlayers().get(0);
		if (!s.placed().around(player.getX(), player.getZ(), 64.0)) {
			return; // not in this level's region yet (just joined, or Sam's player is still elsewhere)
		}

		int pcx = player.blockPosition().getX() >> 4, pcz = player.blockPosition().getZ() >> 4;
		queueNear(s, pcx, pcz);
		placeDone(s, server.overworld());

		if (!announced && readyAround(s, pcx, pcz)) {
			announced = true;
			SemCraft.LOG.info(String.format(Locale.ROOT, "level %s ready around the player: %d shell blocks so far (%.1f s)",
				s.name(), blocksPlaced, (System.nanoTime() - loadStartNanos) / 1.0E9));
			SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"level_ready\",\"hash\":\"%s\",\"blocks\":%d}", s.hash(), blocksPlaced));
			WorldBridge.syncBlocks(32);
		}
	}

	/** Hand chunks near the player to the workers, nearest rings first. */
	private static void queueNear(final Shell s, final int pcx, final int pcz) {
		for (int r = 0; r <= RADIUS_CHUNKS && jobs < MAX_JOBS; r++) {
			for (int dx = -r; dx <= r && jobs < MAX_JOBS; dx++) {
				for (int dz = -r; dz <= r && jobs < MAX_JOBS; dz++) {
					if (Math.max(Math.abs(dx), Math.abs(dz)) != r) {
						continue;
					}

					int cx = pcx + dx, cz = pcz + dz;
					long chunk = ChunkPos.pack(cx, cz);
					if (!queued.add(chunk)) {
						continue;
					}

					if (!s.placed().anyNear(cx, cz)) {
						placed.add(chunk); // nothing of Sam's level there
						continue;
					}

					jobs++;
					int generation = s.generation();
					WORKERS.execute(() -> {
						Long2ByteOpenHashMap v;
						try {
							v = Voxelizer.voxelizeChunk(s.placed(), cx, cz);
						} catch (RuntimeException e) {
							SemCraft.LOG.warn("voxelising chunk {} {} failed", cx, cz, e);
							v = new Long2ByteOpenHashMap();
						}

						done.add(new Done(generation, chunk, v));
					});
				}
			}
		}
	}

	/** Put finished chunks into the world, within the block budget. */
	private static void placeDone(final Shell s, final ServerLevel level) {
		int budget = BUDGET;
		BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
		for (Done d; budget > 0 && (d = done.poll()) != null;) {
			if (d.generation() != s.generation()) {
				continue; // a previous level's
			}

			jobs--;
			placed.add(d.chunk());
			voxels.putAll(d.voxels());
			WorldBridge.quietGround(true);
			for (Long2ByteMap.Entry e : d.voxels().long2ByteEntrySet()) {
				pos.set(e.getLongKey());
				if (!level.isInWorldBounds(pos)) {
					continue;
				}

				BlockState now = level.getBlockState(pos);
				BlockState want = ShellBlock.withHeight(e.getByteValue());
				if (now.isAir() || ShellBlock.is(now) && now != want) {
					level.setBlock(pos, want, FLAGS);
					blocksPlaced++;
				}
			}

			WorldBridge.quietGround(false);
			budget -= d.voxels().size();
		}
	}

	private static boolean readyAround(final Shell s, final int pcx, final int pcz) {
		for (int dx = -READY_RADIUS; dx <= READY_RADIUS; dx++) {
			for (int dz = -READY_RADIUS; dz <= READY_RADIUS; dz++) {
				if (!placed.contains(ChunkPos.pack(pcx + dx, pcz + dz))) {
					return false;
				}
			}
		}

		return true;
	}

	/** For the dev link: what the shell is doing. */
	public static String status() {
		Shell s = current;
		MinecraftServer server = WorldBridge.server();
		String world = "";
		String sample = "";
		if (server != null) {
			ServerLevel level = server.overworld();
			world = String.format(Locale.ROOT, ",\"minY\":%d,\"height\":%d", level.getMinY(), level.getHeight());
			if (!voxels.isEmpty()) {
				long p = voxels.keySet().iterator().nextLong();
				BlockPos pos = BlockPos.of(p);
				sample = String.format(Locale.ROOT, ",\"sample\":{\"pos\":[%d,%d,%d],\"h\":%d,\"inBounds\":%b,\"state\":\"%s\"}", pos.getX(), pos.getY(), pos.getZ(),
					voxels.get(p), level.isInWorldBounds(pos), level.getBlockState(pos).toString().replace("\"", "'"));
			}
		}

		return String.format(Locale.ROOT, "{\"level\":\"%s\",\"chunksQueued\":%d,\"chunksPlaced\":%d,\"jobs\":%d,\"blocks\":%d,\"voxels\":%d,\"offset\":[%.0f,%.0f,%.0f]%s%s}",
			s == null ? "" : s.name().replace("\\", "/"), queued.size(), placed.size(), jobs, blocksPlaced, voxels.size(), Mapping.ox(), Mapping.oy(), Mapping.oz(),
			world, sample);
	}

	/** The world just opened: build the level Sam announced earlier, if any. */
	static void onServerStarted() {
		String[] w = wanted;
		if (w != null) {
			load(w[0], w[1], w[2]);
		}
	}

	static void detach() {
		current = null;
		pending = null;
		loadingHash = null;
		queued.clear();
		placed.clear();
		voxels.clear();
		done.clear();
		jobs = 0;
	}
}
