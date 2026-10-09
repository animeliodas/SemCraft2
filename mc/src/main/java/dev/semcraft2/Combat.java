package dev.semcraft2;

import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.concurrent.ThreadLocalRandom;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.resources.Identifier;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntitySpawnReason;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.projectile.ProjectileUtil;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

/** Serious Sam's weapons in Minecraft: hitscan shots and explosions hurt Minecraft's mobs. */
public final class Combat {
	private static final double RANGE = 160.0;
	private static int loggedShots;
	/** Explosions of Sam's weapons: blocks and mobs, not the player (Sam's own explosion already hurt Sam). */
	private static final net.minecraft.world.level.ExplosionDamageCalculator NOT_THE_PLAYER = new net.minecraft.world.level.ExplosionDamageCalculator() {
		@Override
		public boolean shouldDamageEntity(final net.minecraft.world.level.Explosion explosion, final Entity entity) {
			return !(entity instanceof ServerPlayer) && !SamMonsters.isProxy(entity) && super.shouldDamageEntity(explosion, entity);
		}
	};

	private Combat() {
	}

	private static ServerPlayer player(final MinecraftServer s) {
		return s.getPlayerList().getPlayers().isEmpty() ? null : s.getPlayerList().getPlayers().get(0);
	}

	/**
	 * Sam fired `rays` hitscan rays from `o` (Sam coords) along `d`, spread over `spreadDeg`, each doing `samDamage`
	 * Sam hit points: the first living thing along each ray, stopped by blocks (Sam's level is blocks too), is hurt.
	 */
	public static void shot(final double[] o, final double[] d, final float samDamage, final int rays, final float spreadDeg, final double range) {
		MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			ServerPlayer player = player(s);
			Vec3 from = new Vec3(Mapping.mcX(o[0]), Mapping.mcY(o[1]), Mapping.mcZ(o[2]));
			Vec3 dir = new Vec3(d[0], d[1], d[2]).normalize();
			ThreadLocalRandom random = ThreadLocalRandom.current();
			double spread = Math.toRadians(spreadDeg);
			for (int i = 0; i < Math.max(1, rays); i++) {
				Vec3 ray = dir;
				if (spread > 0.0) {
					ray = dir.add(random.nextGaussian() * spread * 0.5, random.nextGaussian() * spread * 0.5, random.nextGaussian() * spread * 0.5).normalize();
				}

				Vec3 to = from.add(ray.scale(Math.min(range, RANGE)));
				HitResult block = level.clip(new ClipContext(from, to, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, player));
				if (block.getType() != HitResult.Type.MISS) {
					to = block.getLocation();
				}

				EntityHitResult hit = ProjectileUtil.getEntityHitResult(level, player, from, to, new AABB(from, to).inflate(1.0),
					e -> e instanceof LivingEntity && e.isAlive() && e != player && !e.isSpectator() && !SamMonsters.isProxy(e), 0.3F);
				if (loggedShots < 40) {
					loggedShots++;
					SemCraft.LOG.info(String.format(Locale.ROOT, "shot from %.1f %.1f %.1f dir %.2f %.2f %.2f: block %s at %.1f, entity %s",
						from.x, from.y, from.z, ray.x, ray.y, ray.z, block.getType(), block.getLocation().distanceTo(from),
						hit == null ? "none" : hit.getEntity().getType().toShortString()));
				}

				if (hit != null && hit.getEntity() instanceof LivingEntity target) {
					DamageSource source = player != null ? level.damageSources().playerAttack(player) : level.damageSources().generic();
					target.damageCooldownTime = 0; // a minigun lands several hits inside the usual cooldown
					target.setInvulnerableTime(0);
					target.hurtServer(level, source, samDamage / Mapping.HP_SCALE);
					level.sendParticles(ParticleTypes.DAMAGE_INDICATOR, hit.getLocation().x, hit.getLocation().y, hit.getLocation().z, 2, 0.1, 0.1, 0.1, 0.1);
				}
			}
		});
	}

	/** A Sam explosion at `p` (Sam coords): Minecraft's mobs in `radius` are hurt and thrown, blocks too if `breaks`. */
	public static void boom(final double[] p, final float radius, final float samDamage, final boolean breaks) {
		MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			Vec3 at = new Vec3(Mapping.mcX(p[0]), Mapping.mcY(p[1]), Mapping.mcZ(p[2]));
			if (breaks) {
				// a small Minecraft explosion for the blocks (shell blocks are blast-proof); the player isn't hurt by
				// it here: Sam's explosion hurts Sam's player (or Sam's copy of the projectile is gone and the hits on
				// mobs come below)
				level.explode(null, null, NOT_THE_PLAYER, at.x, at.y, at.z, Math.min(radius * 0.5F, 4.0F), false, ServerLevel.ExplosionInteraction.TNT);
			}

			List<LivingEntity> near = level.getEntitiesOfClass(LivingEntity.class, new AABB(at, at).inflate(radius), e -> !(e instanceof ServerPlayer) && !SamMonsters.isProxy(e));
			for (LivingEntity e : near) {
				double dist = e.position().distanceTo(at);
				if (dist > radius) {
					continue;
				}

				float f = (float) (1.0 - dist / radius);
				e.damageCooldownTime = 0;
				e.setInvulnerableTime(0);
				e.hurtServer(level, level.damageSources().explosion(null, null), samDamage / Mapping.HP_SCALE * f);
				Vec3 push = e.position().subtract(at).normalize().scale(1.2 * f);
				e.push(push.x, 0.4 * f + push.y, push.z);
			}
		});
	}

	/** Sam's projectiles in flight: id -> where they were last tick (Minecraft coordinates). Server thread only. */
	private static final java.util.Map<Long, Vec3> projectiles = new java.util.HashMap<>();

	/**
	 * Sam's rockets, grenades, laser bolts and cannonballs this tick: [id, x, y, z, radius, damage, breaks] each (Sam
	 * coords). Sam doesn't know Minecraft's mobs, so its projectiles would fly through them: one that crosses a mob
	 * between two ticks explodes there, and Sam is told to remove its copy quietly.
	 */
	public static void projectiles(final double[][] list) {
		MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			ServerPlayer player = player(s);
			java.util.Set<Long> seen = new java.util.HashSet<>();
			for (double[] p : list) {
				long id = (long) p[0];
				seen.add(id);
				Vec3 now = new Vec3(Mapping.mcX(p[1]), Mapping.mcY(p[2]), Mapping.mcZ(p[3]));
				Vec3 last = projectiles.getOrDefault(id, now);
				projectiles.put(id, now);
				AABB sweep = new AABB(last, now).inflate(1.0);
				LivingEntity best = null;
				Vec3 bestAt = null;
				double bestD = Double.MAX_VALUE;
				for (LivingEntity e : level.getEntitiesOfClass(LivingEntity.class, sweep, e -> e.isAlive() && e != player && !e.isSpectator() && !SamMonsters.isProxy(e))) {
					java.util.Optional<Vec3> at = e.getBoundingBox().inflate(0.4).clip(last, now);
					if (at.isEmpty() && e.getBoundingBox().inflate(0.4).contains(now)) {
						at = java.util.Optional.of(now);
					}

					if (at.isPresent() && at.get().distanceToSqr(last) < bestD) {
						best = e;
						bestAt = at.get();
						bestD = at.get().distanceToSqr(last);
					}
				}

				if (best != null) {
					projectiles.remove(id);
					seen.remove(id);
					double[] sam = {Mapping.samX(bestAt.x), Mapping.samY(bestAt.y), Mapping.samZ(bestAt.z)};
					SemCraft.LOG.info(String.format(Locale.ROOT, "Sam projectile %d hit %s", id, best.getType().toShortString()));
					boom(sam, (float) p[4], (float) p[5], p[6] != 0.0);
					SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"projhit\",\"id\":%d}", id));
				}
			}

			projectiles.keySet().retainAll(seen);
		});
	}

	/** Test hook: a gold pillar standing on (x, y, z) Sam coords, `h` blocks tall. */
	public static void pillar(final double[] p, final int h) {
		MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			BlockPos base = BlockPos.containing(Mapping.mcX(p[0]), Mapping.mcY(p[1]), Mapping.mcZ(p[2]));
			for (int i = 0; i < h; i++) {
				level.setBlockAndUpdate(base.above(i), Blocks.GOLD_BLOCK.defaultBlockState());
			}

			SemCraft.LOG.info(String.format(Locale.ROOT, "pillar at %s (Sam %.2f %.2f %.2f)", base, p[0], p[1], p[2]));
		});
	}

	/** Test hook / director: spawn `n` mobs of `kind` around (x, y, z) Sam coords, on the shell. */
	public static void spawn(final String kind, final double[] p, final int n, final double radius) {
		MinecraftServer s = WorldBridge.server();
		if (s == null) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			Optional<EntityType<?>> type = BuiltInRegistries.ENTITY_TYPE.getOptional(Identifier.withDefaultNamespace(kind));
			if (type.isEmpty()) {
				SemCraft.LOG.warn("spawn: unknown mob {}", kind);
				return;
			}

			ThreadLocalRandom random = ThreadLocalRandom.current();
			int spawned = 0;
			for (int i = 0; i < n * 6 && spawned < n; i++) {
				double a = random.nextDouble() * Math.PI * 2.0, r = radius * Math.sqrt(random.nextDouble());
				double x = Mapping.mcX(p[0]) + Math.cos(a) * r, z = Mapping.mcZ(p[2]) + Math.sin(a) * r;
				BlockPos ground = groundAt(level, (int) Math.floor(x), (int) Math.floor(Mapping.mcY(p[1])), (int) Math.floor(z));
				if (ground == null) {
					continue;
				}

				Entity e = type.get().spawn(level, ground, EntitySpawnReason.COMMAND);
				if (e instanceof Mob mob) {
					mob.setPersistenceRequired();
					spawned++;
				}
			}

			SemCraft.LOG.info("spawn: {} x {}", spawned, kind);
		});
	}

	/** The free block above solid ground near height y in column (x, z), or null. */
	static BlockPos groundAt(final ServerLevel level, final int x, final int y, final int z) {
		BlockPos.MutableBlockPos p = new BlockPos.MutableBlockPos();
		for (int dy = 4; dy >= -12; dy--) {
			p.set(x, y + dy, z);
			if (!level.getBlockState(p).isAir() && level.getBlockState(p.above()).isAir() && level.getBlockState(p.above(2)).isAir()) {
				return p.above().immutable();
			}
		}

		return null;
	}
}
