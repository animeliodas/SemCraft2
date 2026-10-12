package dev.semcraft2;

import java.util.Locale;
import java.util.Map;
import java.util.WeakHashMap;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;

/**
 * Minecraft's pistons push Sam's player and Sam's monsters. The client simulates pistons pushing its own player (the
 * stand-in, which Sam's camera puts back every frame): what they move it by goes to Sam's player. The server moves
 * Sam's monsters' stand-ins: what they're moved by goes to the monsters. As in Entity.limitPistonMovement, an entity
 * moves at most 0.51 blocks per axis per tick however many piston parts push it.
 */
public final class Pushes {
	private static final double LIMIT = 0.51;
	/** Per entity: the game tick of its pushes so far, and how far they went on each axis. */
	private static final Map<Entity, double[]> pushed = new WeakHashMap<>();

	private Pushes() {
	}

	public static void onPiston(final Entity entity, final Vec3 wanted) {
		if (!SemCraft.active || wanted == null) {
			return;
		}

		boolean client = entity.level().isClientSide();
		boolean player = client && entity instanceof Player;
		if (!player && (client || !SamMonsters.isProxy(entity))) {
			return;
		}

		Vec3 d = limit(entity, wanted);
		if (d.lengthSqr() < 1.0E-8) {
			return;
		}

		if (player) {
			SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"shove\",\"d\":[%.4f,%.4f,%.4f],\"by\":\"piston\"}", d.x, d.y, d.z));
		} else {
			SamMonsters.onPistonPush(entity, d);
		}
	}

	private static synchronized Vec3 limit(final Entity entity, final Vec3 wanted) {
		long tick = entity.level().getGameTime();
		double[] s = pushed.computeIfAbsent(entity, e -> new double[4]);
		if ((long) s[3] != tick) {
			s[0] = s[1] = s[2] = 0.0;
			s[3] = tick;
		}

		double[] w = {wanted.x, wanted.y, wanted.z};
		double[] out = new double[3];
		for (int i = 0; i < 3; i++) {
			double total = Math.clamp(s[i] + w[i], -LIMIT, LIMIT);
			out[i] = total - s[i];
			s[i] = total;
		}

		return new Vec3(out[0], out[1], out[2]);
	}
}
