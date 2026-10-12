package dev.semcraft2;

import it.unimi.dsi.fastutil.longs.Long2IntMap;
import it.unimi.dsi.fastutil.longs.Long2IntOpenHashMap;
import it.unimi.dsi.fastutil.longs.LongLinkedOpenHashSet;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.storage.LevelResource;

/**
 * A clean Minecraft world for every Sam level entered (docs/CONTRACT.md "reset"). Sam says when: a new game resets
 * "all" (blocks, mobs and the player's inventory), the next level or a loaded save resets the "world" (blocks and mobs;
 * the inventory stays).
 *
 * <p>The world is void apart from Sam's shell, so any other block in it was built since, and any entity but the player
 * was spawned since. Each reset starts a new epoch. Loaded chunks are cleaned at once; a chunk last cleaned in an
 * earlier epoch is cleaned when it next loads (levels live kilometres apart, see LevelShell). Entities carry their
 * epoch as a tag, and one from an earlier epoch is removed when it loads. Server thread, except {@link #request}.
 */
public final class WorldReset {
	private static final String FILE = "semcraft2_reset.dat";
	private static final int MAGIC = 0x53435231; // "SCR1"
	private static final String TAG = "semcraft2_epoch_";
	/** No neighbour updates, drops or block entity side effects: the blocks just go. */
	private static final int FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_SKIP_ALL_SIDEEFFECTS;

	private static volatile String requested;
	/** The world's reset state is read (entities and chunks loading before that are left alone). */
	private static boolean attached;
	private static int epoch;
	/** Chunk -> the epoch it was last cleaned (or first seen clean) in. */
	private static final Long2IntOpenHashMap seen = new Long2IntOpenHashMap();
	/** Loaded chunks from an earlier epoch, cleaned on the next ticks. */
	private static final LongLinkedOpenHashSet stale = new LongLinkedOpenHashSet();
	private static boolean changed;
	private static int saveIn = 1200;
	private static long blocksCleared, entitiesCleared, chunksCleaned;
	private static String last = "none";

	private WorldReset() {
	}

	/** Link thread: Sam entered a level ("all" for a new game, "world" otherwise). */
	public static void request(final String what) {
		requested = "all".equals(what) ? "all" : "world";
	}

	static void attach(final MinecraftServer server) {
		seen.clear();
		stale.clear();
		epoch = 0;
		Path file = server.getWorldPath(LevelResource.ROOT).resolve(FILE);
		if (Files.exists(file)) {
			try (DataInputStream in = new DataInputStream(Files.newInputStream(file))) {
				if (in.readInt() == MAGIC) {
					epoch = in.readInt();
					for (int n = in.readInt(); n > 0; n--) {
						seen.put(in.readLong(), in.readInt());
					}
				}
			} catch (IOException | RuntimeException e) {
				SemCraft.LOG.warn("couldn't read {}", file, e);
			}
		}

		attached = true;
		SemCraft.LOG.info("world resets: epoch {}, {} chunks known", epoch, seen.size());
	}

	static void detach() {
		attached = false;
		requested = null;
	}

	static void save(final MinecraftServer server) {
		Path file = server.getWorldPath(LevelResource.ROOT).resolve(FILE);
		try (DataOutputStream out = new DataOutputStream(Files.newOutputStream(file))) {
			out.writeInt(MAGIC);
			out.writeInt(epoch);
			out.writeInt(seen.size());
			for (Long2IntMap.Entry e : seen.long2IntEntrySet()) {
				out.writeLong(e.getLongKey());
				out.writeInt(e.getIntValue());
			}

			changed = false;
		} catch (IOException e) {
			SemCraft.LOG.warn("couldn't write {}", file, e);
		}
	}

	/** A chunk loaded (server thread): one from before the last reset is cleaned on the next tick. */
	static void onChunkLoad(final ServerLevel level, final LevelChunk chunk) {
		if (!attached || level != level.getServer().overworld()) {
			return;
		}

		long key = chunk.getPos().pack();
		// a chunk never seen before is only suspect once there has been a reset (it may hold blocks from before it)
		int e = seen.getOrDefault(key, epoch == 0 ? 0 : -1);
		if (e < epoch) {
			stale.add(key);
		} else if (!seen.containsKey(key)) {
			seen.put(key, epoch);
			changed = true;
		}
	}

	/** An entity loaded or spawned (server thread): one from before the last reset goes; a new one gets this epoch. */
	static void onEntityLoad(final Entity entity, final ServerLevel level) {
		if (!attached || entity instanceof Player || level != level.getServer().overworld()) {
			return;
		}

		String mine = TAG + epoch;
		for (String tag : entity.entityTags()) {
			if (tag.startsWith(TAG) && !tag.equals(mine)) {
				entity.discard();
				entitiesCleared++;
				return;
			}
		}

		entity.addTag(mine);
	}

	static void tick(final MinecraftServer server) {
		ServerLevel level = server.overworld();
		String what = requested;
		if (what != null) {
			requested = null;
			reset(server, level, what);
		}

		// stale chunks that loaded since: a few per tick
		for (int i = 0; i < 16 && !stale.isEmpty(); i++) {
			long key = stale.removeFirstLong();
			LevelChunk chunk = level.getChunkSource().getChunkNow(ChunkPos.getX(key), ChunkPos.getZ(key));
			if (chunk != null) {
				clean(level, chunk);
			}
		}

		if (--saveIn <= 0) {
			saveIn = 1200;
			if (changed) {
				save(server);
			}
		}
	}

	private static void reset(final MinecraftServer server, final ServerLevel level, final String what) {
		long t0 = System.nanoTime();
		long blocks0 = blocksCleared, entities0 = entitiesCleared;
		epoch++;
		changed = true;

		// every loaded chunk now (the rest when they load): the ones seen loading, and the ones around the player (they
		// may have loaded before this world's reset state was read)
		LongLinkedOpenHashSet loaded = new LongLinkedOpenHashSet();
		for (long key : seen.keySet()) {
			if (level.getChunkSource().getChunkNow(ChunkPos.getX(key), ChunkPos.getZ(key)) != null) {
				loaded.add(key);
			}
		}

		if (!server.getPlayerList().getPlayers().isEmpty()) {
			ChunkPos at = server.getPlayerList().getPlayers().get(0).chunkPosition();
			int r = server.getPlayerList().getViewDistance() + 2;
			for (int dx = -r; dx <= r; dx++) {
				for (int dz = -r; dz <= r; dz++) {
					if (level.getChunkSource().getChunkNow(at.x() + dx, at.z() + dz) != null) {
						loaded.add(ChunkPos.pack(at.x() + dx, at.z() + dz));
					}
				}
			}
		}

		stale.addAll(loaded);
		while (!stale.isEmpty()) {
			long key = stale.removeFirstLong();
			LevelChunk chunk = level.getChunkSource().getChunkNow(ChunkPos.getX(key), ChunkPos.getZ(key));
			if (chunk != null) {
				clean(level, chunk);
			}
		}

		// every entity but the player (Sam's monsters' stand-ins come back with Sam's next list)
		List<Entity> gone = new ArrayList<>();
		for (Entity e : level.getAllEntities()) {
			if (!(e instanceof Player) && !e.isRemoved()) {
				gone.add(e);
			}
		}

		gone.forEach(Entity::discard);
		entitiesCleared += gone.size();
		SamMonsters.forget();

		if ("all".equals(what) && !server.getPlayerList().getPlayers().isEmpty()) {
			WorldBridge.restartInventory(server.getPlayerList().getPlayers().get(0));
		}

		save(server);
		last = String.format(Locale.ROOT, "%s: %d blocks, %d entities, %d chunks, %.0f ms", what, blocksCleared - blocks0,
			entitiesCleared - entities0, loaded.size(), (System.nanoTime() - t0) / 1.0E6);
		SemCraft.LOG.info("world reset to epoch {} ({})", epoch, last);
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"log\",\"m\":\"Minecraft world reset (%s)\"}", last));
	}

	/** Not air, not Sam's shell: built since. */
	private static boolean foreign(final BlockState state) {
		return !state.isAir() && !ShellBlock.is(state);
	}

	/** Every block in the chunk that isn't the shell goes (where the shell was, it comes back: WorldBridge.onBlockChanged). */
	private static void clean(final ServerLevel level, final LevelChunk chunk) {
		LevelChunkSection[] sections = chunk.getSections();
		ChunkPos cp = chunk.getPos();
		List<BlockPos> found = new ArrayList<>();
		for (int i = 0; i < sections.length; i++) {
			LevelChunkSection s = sections[i];
			if (s == null || s.hasOnlyAir() || !s.maybeHas(WorldReset::foreign)) {
				continue;
			}

			int y0 = chunk.getSectionYFromSectionIndex(i) << 4;
			for (int y = 0; y < 16; y++) {
				for (int z = 0; z < 16; z++) {
					for (int x = 0; x < 16; x++) {
						if (foreign(s.getBlockState(x, y, z))) {
							found.add(new BlockPos(cp.getMinBlockX() + x, y0 + y, cp.getMinBlockZ() + z));
						}
					}
				}
			}
		}

		for (BlockPos p : found) {
			level.setBlock(p, Blocks.AIR.defaultBlockState(), FLAGS);
		}

		blocksCleared += found.size();
		chunksCleaned++;
		seen.put(cp.pack(), epoch);
		changed = true;
	}

	/** For the dev status. */
	public static String status() {
		return String.format(Locale.ROOT, "{\"epoch\":%d,\"chunks\":%d,\"stale\":%d,\"cleaned\":%d,\"blocks\":%d,\"entities\":%d,\"last\":\"%s\"}",
			epoch, seen.size(), stale.size(), chunksCleaned, blocksCleared, entitiesCleared, last);
	}
}
