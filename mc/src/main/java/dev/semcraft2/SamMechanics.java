package dev.semcraft2;

import java.util.Locale;
import java.util.Map;
import java.util.WeakHashMap;
import net.minecraft.core.Holder;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/**
 * Serious Sam's own mechanics, working in Minecraft: his health above a hundred as more hearts, his armour on the armour
 * bar, his powerups (TSE) as Minecraft effects, his serious bomb on Minecraft's monsters, Minecraft kills in his score,
 * and Minecraft's ender pearls carrying him. Server thread unless said otherwise.
 */
public final class SamMechanics {
	/** Ticks a Sam-made hit (an explosion has no attacker in Minecraft) still counts for the kill. */
	private static final long SAM_HIT_TICKS = 100;
	private static final double SERIOUS_BOMB_RANGE = 96.0;

	/** Sam's armour (0..200), for the client's armour bar (link thread -> render thread). */
	public static volatile float samArmor;
	/** Sam's powerups, seconds left: invisibility, invulnerability, serious damage, serious speed (TSE). */
	private static volatile float[] powerups = new float[4];
	/** The health a Sam level starts with (100; 200 on the easy difficulties). */
	private static volatile float samTop = 100.0F;

	private static final Map<Entity, Long> samHits = new WeakHashMap<>();

	private SamMechanics() {
	}

	/** Link thread: the rest of Sam's state message. */
	public static void state(final float top, final float armor, final float[] pw) {
		samTop = top > 1.0F ? top : 100.0F;
		samArmor = Math.max(armor, 0.0F);
		if (pw != null && pw.length == 4) {
			powerups = pw;
		}
	}

	/**
	 * Hearts: as many as Sam's health needs, never fewer than his level's top health gives (100 = ten hearts; 200 on easy
	 * = twenty), so health picked up beyond the top shows as more hearts, and they go again as it's lost.
	 */
	static float maxHealthFor(final float samHp) {
		float max = Math.max(samTop, samHp) / Mapping.HP_SCALE;
		return (float) Math.ceil(max / 2.0F) * 2.0F;
	}

	static void applyMaxHealth(final ServerPlayer player, final float samHp) {
		AttributeInstance max = player.getAttribute(Attributes.MAX_HEALTH);
		float want = maxHealthFor(samHp);
		if (max != null && Math.abs(max.getBaseValue() - want) > 0.01) {
			max.setBaseValue(want);
		}
	}

	/** Sam's powerups as Minecraft effects on the stand-in (refreshed every second while they last). */
	static void applyPowerups(final ServerPlayer player, final long tick) {
		if (tick % 20 != 0) {
			return;
		}

		float[] pw = powerups;
		effect(player, MobEffects.INVISIBILITY, pw[0], 0);       // Minecraft's mobs barely see him
		effect(player, MobEffects.RESISTANCE, pw[1], 4);         // nothing in Minecraft hurts him
		effect(player, MobEffects.STRENGTH, pw[2], 2);           // his blows hit harder (Sam's guns: x4, sam/Source/Weapons.cpp)
		effect(player, MobEffects.SPEED, pw[3], 1);
	}

	private static void effect(final ServerPlayer player, final Holder<MobEffect> effect, final float secondsLeft, final int amplifier) {
		MobEffectInstance now = player.getEffect(effect);
		if (secondsLeft <= 0.0F) {
			if (now != null && now.getAmplifier() == amplifier && !now.isInfiniteDuration()) {
				player.removeEffect(effect);
			}
			return;
		}

		int ticks = Math.round(secondsLeft * 20.0F);
		if (now == null || Math.abs(now.getDuration() - ticks) > 30) {
			player.addEffect(new MobEffectInstance(effect, ticks, amplifier, false, false, true));
		}
	}

	/** Sam hurt this entity in a way Minecraft doesn't put down to the player (his explosions). */
	public static void samHit(final Entity e) {
		samHits.put(e, e.level().getGameTime());
	}

	/** A living entity died (ServerLivingEntityEvents.AFTER_DEATH): a Minecraft mob Sam killed scores for him. */
	static void onDeath(final LivingEntity dead, final DamageSource source) {
		if (!SemCraft.active || !(dead instanceof Mob) || SamMonsters.isProxy(dead)) {
			return;
		}

		Long hit = samHits.remove(dead);
		boolean bySam = source.getEntity() instanceof ServerPlayer
			|| hit != null && dead.level().getGameTime() - hit <= SAM_HIT_TICKS;
		if (!bySam) {
			return;
		}

		int score = Math.max(100, Math.round(dead.getMaxHealth() * 25.0F / 50.0F) * 50);
		String kind = BuiltInRegistries.ENTITY_TYPE.getKey(dead.getType()).getPath();
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"kill\",\"kind\":\"%s\",\"score\":%d}", kind, score));
	}

	/** Sam set off a serious bomb at p (Sam coordinates; link thread): Minecraft's monsters around are gone too. */
	public static void seriousBomb(final double[] p) {
		MinecraftServer s = WorldBridge.server();
		if (s == null || p == null || p.length < 3) {
			return;
		}

		s.execute(() -> {
			ServerLevel level = s.overworld();
			Vec3 at = new Vec3(Mapping.mcX(p[0]), Mapping.mcY(p[1]), Mapping.mcZ(p[2]));
			ServerPlayer player = s.getPlayerList().getPlayers().isEmpty() ? null : s.getPlayerList().getPlayers().get(0);
			int killed = 0;
			for (LivingEntity e : level.getEntitiesOfClass(LivingEntity.class, new AABB(at, at).inflate(SERIOUS_BOMB_RANGE),
				e -> e instanceof Enemy && e.isAlive() && !SamMonsters.isProxy(e))) {
				level.sendParticles(ParticleTypes.EXPLOSION_EMITTER, e.getX(), e.getY() + 1.0, e.getZ(), 1, 0.0, 0.0, 0.0, 0.0);
				samHit(e);
				e.hurtServer(level, player != null ? level.damageSources().playerAttack(player) : level.damageSources().magic(), e.getHealth() + 100.0F);
				killed++;
			}

			level.playSound(null, at.x, at.y, at.z, SoundEvents.GENERIC_EXPLODE, SoundSource.PLAYERS, 4.0F, 0.6F);
			SemCraft.LOG.info("serious bomb at {}: {} Minecraft monsters", at, killed);
		});
	}

	/**
	 * An ender pearl the player threw landed (ThrownEnderpearl.onHit): Sam's player goes where it landed, and it hurts
	 * him as it would Steve. Minecraft's own teleport of the stand-in follows anyway.
	 */
	public static void pearlLanded(final Entity pearl, final Entity owner) {
		if (!SemCraft.active || !(owner instanceof ServerPlayer) || pearl.level().isClientSide()) {
			return;
		}

		Vec3 at = pearl.position();
		SemCraft.LOG.info("ender pearl landed at {}", at);
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"teleport\",\"p\":[%.3f,%.3f,%.3f],\"d\":%.1f,\"by\":\"ender_pearl\"}",
			Mapping.samX(at.x), Mapping.samY(at.y), Mapping.samZ(at.z), 5.0F * Mapping.HP_SCALE));
	}

	/** For the dev status. */
	static String status() {
		float[] pw = powerups;
		return String.format(Locale.ROOT, "{\"top\":%.0f,\"armor\":%.0f,\"powerups\":[%.0f,%.0f,%.0f,%.0f]}", samTop, samArmor, pw[0], pw[1], pw[2], pw[3]);
	}
}
