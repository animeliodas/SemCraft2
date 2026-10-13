package dev.semcraft2;

import java.util.List;
import java.util.Locale;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;

/** Server-side half: commands, the player's health mirror, and the shell's upkeep. */
public final class WorldBridge {
	private static volatile MinecraftServer server;
	/** Sam's player state (link thread -> server tick). */
	private static volatile float samHp = 100.0F;
	private static volatile boolean samAlive = true;
	/** While placing the shell, block changes aren't the player's (server thread only). */
	private static boolean quietGround;
	/** Server ticks until the setup commands run (the player isn't in the player list yet when JOIN fires). */
	private static int setupIn = -1;
	private static int refillIn;

	/** Run once the player has joined: a survival player (mobs hunt them) whose hits go to Sam. */
	private static final List<String> SETUP = List.of(
		"gamerule advance_time false",
		"gamerule advance_weather false",
		"gamerule spawn_mobs false",
		"gamerule spawn_monsters false",
		"gamerule spawn_patrols false",
		"gamerule spawn_phantoms false",
		"gamerule spawn_wandering_traders false",
		"gamerule send_command_feedback false",
		"gamerule log_admin_commands false",
		"gamerule keep_inventory true",
		"gamerule show_advancement_messages false",
		"gamerule player_movement_check false",
		"gamerule natural_health_regeneration false",
		"gamerule fall_damage false",
		"difficulty normal",
		"time set noon",
		"weather clear",
		"gamemode survival @a",
		"effect give @a minecraft:saturation infinite 0 true",
		// Sam's levels are big and Sam's camera is at eye height: build where you aim, far away
		"attribute @p minecraft:block_interaction_range base set 48",
		"attribute @p minecraft:entity_interaction_range base set 6"
	);

	/**
	 * The starter kit, given once (and again by a new game, see {@link WorldReset}); after that the player's choices
	 * stay (and are topped up, see refill). Wings for Sam: jump again in the air to glide, a firework in hand + right
	 * mouse button to speed up.
	 */
	static final List<String> STARTER_KIT = List.of(
		"item replace entity @a armor.chest with minecraft:elytra",
		"item replace entity @a weapon.offhand with minecraft:firework_rocket 64",
		"item replace entity @a hotbar.0 with minecraft:diamond_sword",
		"item replace entity @a hotbar.1 with minecraft:grass_block 64",
		"item replace entity @a hotbar.2 with minecraft:stone_bricks 64",
		"item replace entity @a hotbar.3 with minecraft:oak_planks 64",
		"item replace entity @a hotbar.4 with minecraft:glass 64",
		"item replace entity @a hotbar.5 with minecraft:tnt 64",
		"item replace entity @a hotbar.6 with minecraft:flint_and_steel",
		"item replace entity @a hotbar.7 with minecraft:zombie_spawn_egg 64",
		"item replace entity @a hotbar.8 with minecraft:creeper_spawn_egg 64"
	);

	private WorldBridge() {
	}

	static void attach(final MinecraftServer s) {
		server = s;
		WorldReset.attach(s);
		LevelShell.onServerStarted();
	}

	static void detach() {
		MinecraftServer s = server;
		if (s != null) {
			Movers.clearAll(s.overworld()); // the world is saved after this: no doors left standing where they were
			WorldReset.save(s);
		}

		server = null;
		WorldReset.detach();
		LevelShell.detach();
		SamMonsters.detach();
	}

	public static MinecraftServer server() {
		return server;
	}

	/** A player joined: set the world up shortly. */
	static void onJoin(final ServerPlayer player) {
		player.getAbilities().mayfly = true;
		player.getAbilities().flying = true;
		player.onUpdateAbilities();
		setupIn = 10;
	}

	/** Run a command as the server. Results go to the log, not to chat. */
	public static void command(final String command) {
		MinecraftServer s = server;
		if (s == null) {
			return;
		}

		s.execute(() -> {
			SemCraft.LOG.info("command: {}", command);
			s.getCommands().performPrefixedCommand(s.createCommandSourceStack(), command);
		});
	}

	/** For the dev link: the player, Sam's state and the shell (called from the link thread; reads are racy but harmless). */
	public static String status() {
		MinecraftServer s = server;
		ServerPlayer p = s == null || s.getPlayerList().getPlayers().isEmpty() ? null : s.getPlayerList().getPlayers().get(0);
		String player = p == null ? "null" : String.format(Locale.ROOT, "{\"mc\":[%.2f,%.2f,%.2f],\"sam\":[%.2f,%.2f,%.2f],\"health\":%.1f,\"maxHealth\":%.1f,\"effects\":\"%s\"}",
			p.getX(), p.getY(), p.getZ(), Mapping.samX(p.getX()), Mapping.samY(p.getY()), Mapping.samZ(p.getZ()), p.getHealth(), p.getMaxHealth(),
			p.getActiveEffects().stream().map(e -> e.getEffect().getRegisteredName() + "/" + e.getAmplifier()).reduce((a, b) -> a + " " + b).orElse(""));
		long mobs = 0;
		if (s != null) {
			for (net.minecraft.world.entity.Entity e : s.overworld().getAllEntities()) {
				if (e instanceof net.minecraft.world.entity.Mob) {
					mobs++;
				}
			}
		}

		return String.format(Locale.ROOT, "{\"t\":\"status\",\"active\":%b,\"build\":%b,\"samHp\":%.1f,\"player\":%s,\"mobs\":%d,\"samMonsters\":%d,\"shell\":%s,\"movers\":%s,\"reset\":%s,\"sam\":%s}",
			SemCraft.active, SemCraft.buildMode, samHp, player, mobs, SamMonsters.count(), LevelShell.status(), Movers.status(), WorldReset.status(),
			SamMechanics.status());
	}

	/** Where Sam's player stands (Minecraft coordinates), from the camera messages; null until the first one. */
	private static volatile double[] samFeet;

	public static void samFeet(final double x, final double y, final double z) {
		samFeet = new double[] {x, y, z};
	}

	/** The server's player follows Sam's: the client moves it every frame, but far jumps (level changes) go here. */
	private static void followSam(final ServerPlayer player) {
		double[] f = samFeet;
		if (f == null) {
			return;
		}

		if (player.distanceToSqr(f[0], f[1], f[2]) > 4.0 * 4.0) {
			player.teleportTo(f[0], f[1], f[2]);
			player.setDeltaMovement(net.minecraft.world.phys.Vec3.ZERO);
		}
	}

	/** Link thread: Sam's player state. */
	public static void samState(final float hp, final boolean alive) {
		samHp = hp;
		samAlive = alive;
	}

	static void quietGround(final boolean on) {
		quietGround = on;
	}

	static void tick(final MinecraftServer s) {
		if (setupIn > 0 && --setupIn == 0) {
			SETUP.forEach(WorldBridge::command);
			if (!s.getPlayerList().getPlayers().isEmpty() && s.getPlayerList().getPlayers().get(0).getInventory().isEmpty()) {
				STARTER_KIT.forEach(WorldBridge::command); // a new world (an old one keeps the player's things)
			}
			refillIn = 200;
		}

		if (refillIn > 0 && --refillIn == 0) {
			refillIn = 200;
			if (SemCraft.active && !s.getPlayerList().getPlayers().isEmpty()) {
				refill(s.getPlayerList().getPlayers().get(0));
			}
		}

		if (SemCraft.active && !s.getPlayerList().getPlayers().isEmpty()) {
			ServerPlayer player = s.getPlayerList().getPlayers().get(0);
			followSam(player);
			mirrorHealth(player);
			SamMechanics.applyPowerups(player, s.overworld().getGameTime());
		}

		gear(s);
		WorldReset.tick(s);
		LevelShell.tick(s);
		Movers.tick(s.overworld(), s.getPlayerList().getPlayers().isEmpty() ? null : s.getPlayerList().getPlayers().get(0));
		SamMonsters.tick(s);
		Combat.tick(s.overworld());
		flushBlocks();
	}

	/**
	 * Sam opened Minecraft's inventory (link -> client thread): the player is creative while it's open (the creative
	 * inventory has every block and item), then a survival player again, whom mobs hunt.
	 */
	public static void inventoryMode(final boolean open) {
		MinecraftServer s = server;
		if (s == null) {
			return;
		}

		s.execute(() -> {
			if (s.getPlayerList().getPlayers().isEmpty()) {
				return;
			}

			ServerPlayer player = s.getPlayerList().getPlayers().get(0);
			player.setGameMode(open ? net.minecraft.world.level.GameType.CREATIVE : net.minecraft.world.level.GameType.SURVIVAL);
			player.getAbilities().mayfly = true;
			player.getAbilities().flying = true;
			player.onUpdateAbilities();
			SemCraft.LOG.info("inventory {}: {}", open ? "open" : "closed", player.gameMode.getGameModeForPlayer());
		});
	}

	private static String lastGear = "";
	private static int gearIn, gearRepeat;

	/**
	 * What Sam's glide needs to know: an elytra on the chest, a firework in a hand (only that one can be lit), how many
	 * fireworks in all (sent on change, and every 2 s).
	 */
	private static void gear(final MinecraftServer s) {
		if (--gearIn > 0 || s.getPlayerList().getPlayers().isEmpty()) {
			return;
		}

		ServerPlayer player = s.getPlayerList().getPlayers().get(0);
		int fireworks = 0;
		for (int i = 0; i < player.getInventory().getContainerSize(); i++) {
			net.minecraft.world.item.ItemStack stack = player.getInventory().getItem(i);
			if (stack.is(net.minecraft.world.item.Items.FIREWORK_ROCKET)) {
				fireworks += stack.getCount();
			}
		}

		String now = String.format(Locale.ROOT, "{\"t\":\"gear\",\"elytra\":%b,\"hand\":%b,\"fireworks\":%d}",
			player.getItemBySlot(net.minecraft.world.entity.EquipmentSlot.CHEST).is(net.minecraft.world.item.Items.ELYTRA),
			fireworkHand(player) != null, fireworks);
		gearIn = 5;
		// on a change, and every 2 s anyway (Sam may have connected since, or restarted)
		if (!now.equals(lastGear) || ++gearRepeat >= 8) {
			gearRepeat = 0;
			lastGear = now;
			SemCraft.events.accept(now);
		}
	}

	/** The hand holding a firework rocket (the main hand first, as Minecraft uses them), or null. */
	private static net.minecraft.world.InteractionHand fireworkHand(final ServerPlayer player) {
		for (net.minecraft.world.InteractionHand hand : net.minecraft.world.InteractionHand.values()) {
			if (player.getItemInHand(hand).is(net.minecraft.world.item.Items.FIREWORK_ROCKET)) {
				return hand;
			}
		}

		return null;
	}

	/**
	 * Sam lit a firework while gliding (link thread): the rocket in hand is used up as Minecraft's own use would (one
	 * fewer, unless creative), and it flies off with the player: its trail, bang and sound are Minecraft's. Sam's glide
	 * does the boost itself.
	 */
	public static void firework() {
		MinecraftServer s = server;
		if (s == null) {
			return;
		}

		s.execute(() -> {
			if (s.getPlayerList().getPlayers().isEmpty()) {
				return;
			}

			ServerPlayer player = s.getPlayerList().getPlayers().get(0);
			net.minecraft.world.InteractionHand hand = fireworkHand(player);
			if (hand == null) {
				SemCraft.LOG.info("firework: none in hand");
				return;
			}

			net.minecraft.world.item.ItemStack stack = player.getItemInHand(hand);
			ServerLevel level = player.level();
			level.addFreshEntity(new net.minecraft.world.entity.projectile.FireworkRocketEntity(level, stack.copyWithCount(1), player));
			stack.consume(1, player);
			gearIn = 0; // the count changed: tell Sam now
			SemCraft.LOG.info("firework from the {}: {} left there", hand == net.minecraft.world.InteractionHand.MAIN_HAND ? "main hand" : "off hand",
				player.getItemInHand(hand).getCount());
		});
	}

	/**
	 * Blocks never run out: every stackable thing in the hotbar is topped up to a full stack (whatever the player chose).
	 * The off hand isn't: fireworks there get used up (more are in the inventory, E).
	 */
	private static void refill(final ServerPlayer player) {
		net.minecraft.world.entity.player.Inventory inventory = player.getInventory();
		for (int i = 0; i < net.minecraft.world.entity.player.Inventory.getSelectionSize(); i++) {
			net.minecraft.world.item.ItemStack stack = inventory.getItem(i);
			if (!stack.isEmpty() && stack.isStackable() && stack.getCount() < stack.getMaxStackSize()) {
				stack.setCount(stack.getMaxStackSize());
			}
		}
	}

	/** A new game (WorldReset, server thread): the player starts again with the starter kit only. */
	static void restartInventory(final ServerPlayer player) {
		player.getInventory().clearContent();
		player.containerMenu.setCarried(net.minecraft.world.item.ItemStack.EMPTY);
		player.removeAllEffects();
		player.setExperienceLevels(0);
		player.setExperiencePoints(0);
		SETUP.stream().filter(c -> c.startsWith("effect")).forEach(WorldBridge::command);
		STARTER_KIT.forEach(WorldBridge::command);
		gearIn = 0;
	}

	/**
	 * Sam's chat (link thread): "/..." runs as a command at the player, with operator rights, its output shown in
	 * Minecraft's chat; anything else is said in Minecraft's chat as Sam.
	 */
	public static void chat(final String text) {
		MinecraftServer s = server;
		if (s == null || text == null || text.isBlank()) {
			return;
		}

		s.execute(() -> {
			ServerPlayer player = s.getPlayerList().getPlayers().isEmpty() ? null : s.getPlayerList().getPlayers().get(0);
			if (!text.startsWith("/")) {
				s.getPlayerList().broadcastSystemMessage(net.minecraft.network.chat.Component.literal("<Sam> " + text), false);
				return;
			}

			net.minecraft.commands.CommandSource out = new net.minecraft.commands.CommandSource() {
				@Override
				public void sendSystemMessage(final net.minecraft.network.chat.Component message) {
					SemCraft.LOG.info("command output: {}", message.getString());
					if (player != null) {
						player.sendSystemMessage(message);
					}
				}

				@Override
				public boolean acceptsSuccess() {
					return true;
				}

				@Override
				public boolean acceptsFailure() {
					return true;
				}

				@Override
				public boolean shouldInformAdmins() {
					return false;
				}
			};

			net.minecraft.commands.CommandSourceStack source = s.createCommandSourceStack().withSource(out);
			if (player != null) {
				source = source.withEntity(player).withPosition(player.position()).withRotation(player.getRotationVector());
			}

			SemCraft.LOG.info("Sam runs a command: {}", text);
			s.getCommands().performPrefixedCommand(source, text.substring(1));
		});
	}

	/**
	 * Minecraft's hearts show Sam's health (100 Sam = 20 Minecraft = ten hearts; more hearts above his level's top
	 * health, see SamMechanics), never letting the stand-in die.
	 */
	private static void mirrorHealth(final ServerPlayer player) {
		SamMechanics.applyMaxHealth(player, samHp);
		float want = Math.clamp(samHp / Mapping.HP_SCALE, 1.0F, player.getMaxHealth());
		if (!samAlive) {
			want = 1.0F;
		}

		if (Math.abs(player.getHealth() - want) > 0.01F && player.isAlive()) {
			player.setHealth(want);
		}
	}

	/**
	 * ServerPlayer.hurtServer took `taken` health (vanilla worked out armour, invulnerability frames and difficulty):
	 * Sam's player takes it instead.
	 */
	public static void onPlayerHurt(final ServerPlayer player, final DamageSource source, final float taken) {
		if (!SemCraft.active || taken <= 0.0F) {
			return;
		}

		// only what Minecraft's world does to Sam: mobs, arrows, explosions, fire and lava. Not the stand-in's own
		// troubles: it stands inside Sam's level shell near walls (suffocation), flies through the void between
		// levels (out of world), never really falls.
		boolean fromWorld = source.getEntity() != null || source.getDirectEntity() != null
			|| source.is(net.minecraft.tags.DamageTypeTags.IS_EXPLOSION) || source.is(net.minecraft.tags.DamageTypeTags.IS_FIRE);
		if (!fromWorld) {
			return;
		}

		Entity from = source.getEntity() != null ? source.getEntity() : source.getDirectEntity();
		String kind = from != null ? net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE.getKey(from.getType()).getPath() : source.getMsgId();
		double fx = from != null ? Mapping.samX(from.getX()) : Mapping.samX(player.getX());
		double fy = from != null ? Mapping.samY(from.getY()) : Mapping.samY(player.getY());
		double fz = from != null ? Mapping.samZ(from.getZ()) : Mapping.samZ(player.getZ());
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"hurt\",\"d\":%.2f,\"src\":\"%s\",\"from\":[%.3f,%.3f,%.3f]}",
			taken * Mapping.HP_SCALE, kind, fx, fy, fz));
	}

	/** Level.setBlock on the server: a block the player removed from the shell's voxel is put back. */
	public static void onBlockChanged(final ServerLevel level, final BlockPos pos, final BlockState state) {
		if (quietGround || level != level.getServer().overworld() || ShellBlock.is(state)) {
			return;
		}

		// a redstone lamp lighting up next to one of Sam's doors or lifts sets it going
		if (state.getBlock() == net.minecraft.world.level.block.Blocks.REDSTONE_LAMP
			&& state.getValue(net.minecraft.world.level.block.RedstoneLampBlock.LIT)) {
			Movers.onLampLit(level, pos.immutable());
		}

		// what Sam must know: is there a solid Minecraft block here now?
		blockChanges.put(pos.immutable(), !state.isAir() && !state.getCollisionShape(level, pos).isEmpty());
		if (!state.isAir()) {
			return;
		}

		int h = LevelShell.shellHeight(pos);
		if (h > 0) {
			BlockPos p = pos.immutable();
			level.getServer().execute(() -> {
				if (level.getBlockState(p).isAir()) {
					quietGround = true;
					level.setBlock(p, ShellBlock.withHeight(h), Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE);
					quietGround = false;
				}
			});
		}
	}

	/** Block changes this tick (server thread): true = solid now, false = gone. Flushed to Sam once per tick. */
	private static final java.util.Map<BlockPos, Boolean> blockChanges = new java.util.LinkedHashMap<>();
	private static int loggedFlushes;

	/** End of each server tick: {"t":"blocks","set":[x,y,z,...],"clear":[...]} (Sam coordinates of block corners). */
	private static void flushBlocks() {
		if (blockChanges.isEmpty() || !SemCraft.active) {
			blockChanges.clear();
			return;
		}

		StringBuilder set = new StringBuilder(), clear = new StringBuilder();
		for (java.util.Map.Entry<BlockPos, Boolean> e : blockChanges.entrySet()) {
			StringBuilder b = e.getValue() ? set : clear;
			BlockPos p = e.getKey();
			b.append(b.isEmpty() ? "" : ",").append(Math.round(Mapping.samX(p.getX()))).append(',')
				.append(Math.round(Mapping.samY(p.getY()))).append(',').append(Math.round(Mapping.samZ(p.getZ())));
		}

		if (loggedFlushes++ < 20) {
			SemCraft.LOG.info("blocks to Sam: {} chars set, {} chars clear", set.length(), clear.length());
		}

		blockChanges.clear();
		SemCraft.events.accept("{\"t\":\"blocks\",\"set\":[" + set + "],\"clear\":[" + clear + "]}");
	}

	/** Every solid Minecraft block (not Sam's shell) within `radius` of the player, sent to Sam (server thread). */
	public static void syncBlocks(final int radius) {
		MinecraftServer s = server;
		if (s == null || s.getPlayerList().getPlayers().isEmpty()) {
			return;
		}

		ServerLevel level = s.overworld();
		BlockPos c = s.getPlayerList().getPlayers().get(0).blockPosition();
		BlockPos.MutableBlockPos p = new BlockPos.MutableBlockPos();
		int found = 0;
		for (int x = -radius; x <= radius && found < 1500; x++) {
			for (int z = -radius; z <= radius && found < 1500; z++) {
				for (int y = -radius / 2; y <= radius / 2; y++) {
					p.set(c.getX() + x, c.getY() + y, c.getZ() + z);
					if (!level.isInWorldBounds(p) || !level.isLoaded(p)) {
						continue;
					}

					BlockState st = level.getBlockState(p);
					if (!st.isAir() && !ShellBlock.is(st) && !st.getCollisionShape(level, p).isEmpty()) {
						blockChanges.put(p.immutable(), true);
						found++;
					}
				}
			}
		}

		LOG_BLOCKS.info("block sync: {} solid Minecraft blocks near the player", found);
	}

	private static final org.slf4j.Logger LOG_BLOCKS = SemCraft.LOG;
}
