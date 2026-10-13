package dev.semcraft2;

import dev.semcraft2.mixin.MobAccessor;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.tags.DamageTypeTags;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntitySpawnReason;
import net.minecraft.world.entity.EntityTypes;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.ai.goal.GoalSelector;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.npc.villager.Villager;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.Projectile;

/**
 * Serious Sam's monsters in Minecraft. Sam lists the living monsters near its player; each gets an invisible, AI-less
 * villager stand-in ("proxy") here, scaled to the monster's height, that Minecraft's hostile mobs hunt. Any hit on a
 * proxy (a mob, the player's sword, an arrow, TNT, a creeper) is cancelled here and goes to Sam, which hurts the real
 * monster. In return a proxy standing next to a Minecraft mob hits it, so Sam's monsters fight back.
 * (After universal-modder's MIT minecraft-gta5-passthrough MobWar.)
 */
public final class SamMonsters {
	public static final String PROXY_TAG = "sam_monster";
	private static final double FOLLOW_RANGE = 48.0;
	/** Proxy hits on a Minecraft mob: Sam hit points per second of close combat. */
	private static final float MELEE_SAM_DAMAGE = 20.0F;

	/** The newest list from Sam, [id, x, y, z, height, width, health] each (link thread -> server tick). */
	private static final AtomicReference<double[][]> latest = new AtomicReference<>();
	private static volatile long latestNanos;

	// server thread only
	private static final Map<Long, Villager> proxies = new HashMap<>();
	private static final Map<Integer, Long> idOf = new HashMap<>();
	private static final Map<Long, Integer> missing = new HashMap<>();
	private static int meleeIn;
	private static int logged, loggedLists;

	private SamMonsters() {
	}

	/** A Sam monster's stand-in (the server knows its tag; the client sees an invisible villager). */
	public static boolean isProxy(final Entity e) {
		return e instanceof Villager && (e.entityTags().contains(PROXY_TAG) || e.isInvisible() && e.isNoGravity());
	}

	/** Hostile, goal-driven mobs join the fight (brain-driven ones pick targets from memories, not goals). */
	private static boolean fighter(final Entity e) {
		if (!(e instanceof Mob) || !(e instanceof Enemy) || isProxy(e)) {
			return false;
		}

		String id = BuiltInRegistries.ENTITY_TYPE.getKey(e.getType()).getPath();
		return !id.equals("piglin") && !id.equals("piglin_brute") && !id.equals("hoglin") && !id.equals("zoglin")
			&& !id.equals("breeze") && !id.equals("creaking") && !id.equals("warden");
	}

	public static void update(final double[][] list) {
		latest.set(list);
		latestNanos = System.nanoTime();
	}

	/** New hostile mobs hunt Sam's monsters too; stale proxies from an earlier session are removed. */
	static void onEntityLoad(final Entity e, final ServerLevel level) {
		if (e instanceof Villager v && v.entityTags().contains(PROXY_TAG) && !proxies.containsValue(v)) {
			v.discard();
			return;
		}

		if (!fighter(e)) {
			return;
		}

		Mob mob = (Mob) e;
		GoalSelector targets = ((MobAccessor) mob).semcraft2$targetSelector();
		if (targets.getAvailableGoals().stream().noneMatch(w -> w.getGoal() instanceof ProxyTargetGoal)) {
			targets.addGoal(2, new ProxyTargetGoal(mob));
		}

		AttributeInstance range = mob.getAttribute(Attributes.FOLLOW_RANGE);
		if (range != null && range.getBaseValue() < FOLLOW_RANGE) {
			range.setBaseValue(FOLLOW_RANGE);
		}
	}

	/** A proxy was hurt (server thread, LivingEntity.hurtServer; the damage itself is cancelled). */
	public static void onProxyHurt(final LivingEntity proxy, final DamageSource source, final float amount) {
		Long id = idOf.get(proxy.getId());
		Entity attacker = source.getEntity();
		boolean explosion = source.is(DamageTypeTags.IS_EXPLOSION);
		if (id == null || attacker == null && !explosion || isProxy(attacker)) {
			return; // falling, suffocating in the shell, another monster: not a fight
		}

		if (source.getDirectEntity() instanceof Projectile projectile) {
			projectile.discard(); // the arrow ends in the monster
		}

		String kind = attacker == null ? "explosion" : attacker instanceof Player ? "player"
			: BuiltInRegistries.ENTITY_TYPE.getKey(attacker.getType()).getPath();
		net.minecraft.world.phys.Vec3 kb = knockback(proxy, source, amount, explosion);
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"actorhit\",\"id\":%d,\"d\":%.2f,\"boom\":%b,\"from\":\"%s\",\"kb\":[%.2f,%.2f,%.2f]}",
			id, amount * Mapping.HP_SCALE, explosion, kind, kb.x, kb.y, kb.z));
	}

	/**
	 * The knockback Minecraft would give the hit monster (LivingEntity.knockback: 0.4 blocks a tick from a blow, less
	 * from a projectile; an explosion's by how hard it hit), as Sam m/s, away from the hit's source and a little up. A
	 * big monster moves less (by its height: the proxy's scale).
	 */
	private static net.minecraft.world.phys.Vec3 knockback(final LivingEntity proxy, final DamageSource source, final float amount, final boolean explosion) {
		net.minecraft.world.phys.Vec3 from = source.getSourcePosition();
		if (from == null && source.getEntity() != null) {
			from = source.getEntity().position();
		}

		if (from == null) {
			return net.minecraft.world.phys.Vec3.ZERO;
		}

		double dx = proxy.getX() - from.x, dz = proxy.getZ() - from.z;
		double len = Math.sqrt(dx * dx + dz * dz);
		if (len < 1.0E-3) {
			dx = 0.0;
			dz = 0.0;
		} else {
			dx /= len;
			dz /= len;
		}

		double speed = explosion ? Math.min(25.0, amount * 1.5) : source.getDirectEntity() instanceof Projectile ? 5.0 : 8.0; // m/s
		AttributeInstance scale = proxy.getAttribute(Attributes.SCALE);
		speed /= Math.max(1.0, scale == null ? 1.0 : scale.getValue());
		return new net.minecraft.world.phys.Vec3(dx * speed, explosion ? speed * 0.5 : speed * 0.4, dz * speed);
	}

	/** Every server tick. */
	static void tick(final MinecraftServer s) {
		ServerLevel level = s.overworld();
		if (!proxies.isEmpty() && System.nanoTime() - latestNanos > 1_500_000_000L) {
			clear(); // Sam stopped sending
			return;
		}

		double[][] list = latest.getAndSet(null);
		if (list != null) {
			if (list.length > 0 && loggedLists++ < 5) {
				SemCraft.LOG.info("Sam monsters near the player: {}", list.length);
			}

			sync(level, list);
		}

		if (--meleeIn <= 0) {
			meleeIn = 20;
			melee(level);
		}
	}

	private static void sync(final ServerLevel level, final double[][] list) {
		Map<Long, Boolean> seen = new HashMap<>();
		for (double[] a : list) {
			long id = (long) a[0];
			double x = Mapping.mcX(a[1]), y = Mapping.mcY(a[2]), z = Mapping.mcZ(a[3]);
			seen.put(id, true);
			missing.remove(id);
			Villager v = proxies.get(id);
			if (v == null || v.isRemoved()) {
				v = EntityTypes.VILLAGER.create(level, EntitySpawnReason.COMMAND);
				if (v == null) {
					if (logged++ < 20) {
						SemCraft.LOG.warn("Sam monster {}: no villager for its stand-in", id);
					}
					continue;
				}

				v.setInvisible(true);
				v.addEffect(new MobEffectInstance(MobEffects.INVISIBILITY, MobEffectInstance.INFINITE_DURATION, 0, false, false), null);
				v.setNoAi(true);
				v.setNoGravity(true);
				v.setSilent(true);
				v.addTag(PROXY_TAG);
				AttributeInstance scale = v.getAttribute(Attributes.SCALE);
				if (scale != null) {
					scale.setBaseValue(Math.clamp(a[4] / 1.95, 0.4, 8.0));
				}

				v.snapTo(x, y, z, 0.0F, 0.0F);
				proxies.put(id, v); // before adding: the load event must not take it for a stale one
				if (!level.addFreshEntity(v)) {
					proxies.remove(id);
					if (logged++ < 20) {
						SemCraft.LOG.warn("Sam monster {}: the world refused its stand-in at {} {} {}", id, x, y, z);
					}
					continue;
				}

				idOf.put(v.getId(), id);
				if (logged++ < 20) {
					SemCraft.LOG.info(String.format(Locale.ROOT, "Sam monster %d: stand-in at %.1f %.1f %.1f, %.1f high, %s", id, x, y, z, a[4],
						v.isRemoved() ? "removed at once" : "in the world"));
				}
			} else {
				v.setPos(x, y, z);
			}
		}

		// a monster missing from a few lists in a row (dead, out of range) loses its proxy
		for (Iterator<Map.Entry<Long, Villager>> it = proxies.entrySet().iterator(); it.hasNext();) {
			Map.Entry<Long, Villager> e = it.next();
			if (seen.containsKey(e.getKey())) {
				continue;
			}

			int n = missing.merge(e.getKey(), 1, Integer::sum);
			if (n > 3 || e.getValue().isRemoved()) {
				if (logged++ < 20) {
					SemCraft.LOG.info("Sam monster {}: stand-in gone ({})", e.getKey(), e.getValue().isRemoved() ? e.getValue().getRemovalReason() : "not in Sam's list");
				}

				idOf.remove(e.getValue().getId());
				e.getValue().discard();
				missing.remove(e.getKey());
				it.remove();
			}
		}
	}

	/** Sam's monsters hit the Minecraft mobs right next to them, once a second. */
	private static void melee(final ServerLevel level) {
		for (Villager v : proxies.values()) {
			if (v.isRemoved()) {
				continue;
			}

			double reach = v.getBbWidth() * 0.5 + 1.2;
			for (Entity e : level.getEntities(v, v.getBoundingBox().inflate(reach), SamMonsters::fighter)) {
				if (e instanceof LivingEntity mob && mob.isAlive()) {
					mob.hurtServer(level, level.damageSources().mobAttack(v), MELEE_SAM_DAMAGE / Mapping.HP_SCALE);
					break; // one blow each
				}
			}
		}
	}

	/** A piston moved a proxy (server thread, Entity.move): Sam's monster is moved as much. */
	public static void onPistonPush(final Entity proxy, final net.minecraft.world.phys.Vec3 d) {
		Long id = idOf.get(proxy.getId());
		if (id == null || d.lengthSqr() < 1.0E-8) {
			return;
		}

		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"actorpush\",\"id\":%d,\"d\":[%.4f,%.4f,%.4f]}", id, d.x, d.y, d.z));
	}

	/** A reset discarded every entity: the proxies are made again from Sam's next list. */
	static void forget() {
		clear();
	}

	private static void clear() {
		proxies.values().forEach(Entity::discard);
		proxies.clear();
		idOf.clear();
		missing.clear();
	}

	static void detach() {
		clear();
		latest.set(null);
	}

	/** For the dev status. */
	public static int count() {
		return proxies.size();
	}
}
