package dev.semcraft2.client;

import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.player.Abilities;
import net.minecraft.world.phys.Vec3;

/** Keeps the Minecraft player on Serious Sam's: it stands where Sam stands and looks where Sam's camera looks. */
public final class PlayerSync {
	private static final double TELEPORT_SQ = 32.0 * 32.0;
	/** How far Sam's player moved over the last client tick (drives the walk animation). */
	private static float tickDistance;
	private static double lastX = Double.NaN, lastZ;

	private PlayerSync() {
	}

	public static float tickDistance() {
		return tickDistance;
	}

	/**
	 * Where the stand-in stands: with Sam's camera at its player's eyes, so that Minecraft's eyes are exactly Sam's
	 * camera (what Minecraft aims at is what Sam's crosshair shows); in cutscenes, at Sam's player's feet.
	 */
	private static double[] feet(final HostState.Pose p, final LocalPlayer player) {
		double eye = player.getEyeHeight();
		double dx = p.x() - p.px(), dy = p.y() - p.py(), dz = p.z() - p.pz();
		boolean firstPerson = dx * dx + dz * dz < 1.0 && dy > 0.5 && dy < 3.0;
		return firstPerson ? new double[] {p.x(), p.y() - eye, p.z()} : new double[] {p.px(), p.py(), p.pz()};
	}

	/** Every frame, before the camera update: position and rotation to match Sam, always first person. */
	public static void frame() {
		HostState.Pose p = HostState.frame();
		Minecraft minecraft = Minecraft.getInstance();
		LocalPlayer player = minecraft.player;
		if (p == null || player == null) {
			return;
		}

		player.setYRot(p.yaw());
		player.setXRot(p.pitch());
		player.yRotO = p.yaw();
		player.xRotO = p.pitch();
		player.yHeadRot = player.yHeadRotO = p.yaw();
		player.yBodyRot = player.yBodyRotO = p.yaw();
		double[] f = feet(p, player);
		player.setPos(f[0], f[1], f[2]);
		player.xo = player.xOld = f[0];
		player.yo = player.yOld = f[1];
		player.zo = player.zOld = f[2];
		if (minecraft.options.getCameraType() != CameraType.FIRST_PERSON) {
			minecraft.options.setCameraType(CameraType.FIRST_PERSON);
		}
	}

	/** Every client tick, at the start of the player's tick: feet at Sam's player, no motion of its own. */
	public static void tick(final LocalPlayer player) {
		HostState.Pose p = HostState.live();
		if (p == null) {
			return;
		}

		double[] f = feet(p, player);
		tickDistance = Double.isNaN(lastX) ? 0.0F : (float) Math.min(Math.hypot(f[0] - lastX, f[2] - lastZ), 1.0);
		lastX = f[0];
		lastZ = f[2];
		boolean teleport = player.distanceToSqr(f[0], f[1], f[2]) > TELEPORT_SQ;
		player.setPos(f[0], f[1], f[2]);
		if (teleport) {
			player.xo = player.xOld = f[0];
			player.yo = player.yOld = f[1];
			player.zo = player.zOld = f[2];
		}

		player.setDeltaMovement(Vec3.ZERO);
		Abilities abilities = player.getAbilities();
		if (abilities.mayfly && !abilities.flying) {
			abilities.flying = true;
			player.onUpdateAbilities();
		}
	}
}
