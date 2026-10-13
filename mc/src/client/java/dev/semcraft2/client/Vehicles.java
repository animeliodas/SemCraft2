package dev.semcraft2.client;

import com.google.gson.JsonObject;
import dev.semcraft2.Mapping;
import dev.semcraft2.SemCraft;
import java.util.Locale;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.entity.Entity;

/**
 * Minecraft's vehicles carry Serious Sam's player (docs/TRANSPORT.md in SeriousBridge). The vehicle is Minecraft's: a
 * boat on its water, a minecart on its rails, a horse. While the stand-in rides one, Minecraft places it (not Sam's
 * camera), and every tick Sam hears where the seat is ({"t":"ride"}); Sam's player follows it (the core's physics::Ride)
 * and Sam's movement keys steer it ({"t":"steer"} -> Minecraft's own movement keys, so each vehicle steers as it
 * does in Minecraft). Getting on is Minecraft's (right click on it in build mode), getting off too (crouch). Client
 * thread.
 */
public final class Vehicles {
	private static boolean riding;
	private static double lastX, lastY, lastZ;
	private static int loggedSteers;

	private Vehicles() {
	}

	/** The stand-in rides something: Minecraft places it, Sam's camera doesn't (PlayerSync). */
	public static boolean riding(final LocalPlayer player) {
		return player != null && player.isPassenger() && player.getVehicle() != null;
	}

	/** Every client tick. */
	static void tick(final Minecraft mc) {
		LocalPlayer player = mc.player;
		boolean now = SemCraft.active && riding(player);
		if (now) {
			Entity vehicle = player.getVehicle();
			// the seat's velocity from how far it went this tick (a server-driven vehicle's own is only an estimate here)
			double vx = riding ? (player.getX() - lastX) * 20.0 : 0.0;
			double vy = riding ? (player.getY() - lastY) * 20.0 : 0.0;
			double vz = riding ? (player.getZ() - lastZ) * 20.0 : 0.0;
			if (!riding) {
				SemCraft.LOG.info("riding a {}", BuiltInRegistries.ENTITY_TYPE.getKey(vehicle.getType()).getPath());
			}

			SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"ride\",\"on\":true,\"kind\":\"%s\",\"p\":[%.3f,%.3f,%.3f],\"v\":[%.3f,%.3f,%.3f],\"yaw\":%.2f}",
				BuiltInRegistries.ENTITY_TYPE.getKey(vehicle.getType()).getPath(), Mapping.samX(player.getX()), Mapping.samY(player.getY()),
				Mapping.samZ(player.getZ()), vx, vy, vz, vehicle.getYRot()));
		} else if (riding) {
			SemCraft.LOG.info("off the vehicle");
			SemCraft.events.accept("{\"t\":\"ride\",\"on\":false}");
			release(mc);
		}

		riding = now;
		if (player != null) {
			lastX = player.getX();
			lastY = player.getY();
			lastZ = player.getZ();
		}
	}

	/** {"t":"steer","f":-1..1,"s":-1..1,"jump":b,"down":b} from Sam's movement keys, while riding. */
	static void steer(final Minecraft mc, final JsonObject m) {
		double f = m.has("f") ? m.get("f").getAsDouble() : 0.0, s = m.has("s") ? m.get("s").getAsDouble() : 0.0;
		set(mc.options.keyUp, f > 0.3);
		set(mc.options.keyDown, f < -0.3);
		set(mc.options.keyLeft, s < -0.3);
		set(mc.options.keyRight, s > 0.3);
		set(mc.options.keyJump, m.has("jump") && m.get("jump").getAsBoolean());
		set(mc.options.keyShift, m.has("down") && m.get("down").getAsBoolean());
		if (loggedSteers++ < 20) {
			SemCraft.LOG.info("steer: forward {}, side {}, jump {}, down {}", f, s, m.has("jump") && m.get("jump").getAsBoolean(),
				m.has("down") && m.get("down").getAsBoolean());
		}
	}

	private static void set(final KeyMapping key, final boolean down) {
		if (key.isDown() != down) {
			key.setDown(down);
		}
	}

	private static void release(final Minecraft mc) {
		for (KeyMapping key : new KeyMapping[] {mc.options.keyUp, mc.options.keyDown, mc.options.keyLeft, mc.options.keyRight,
			mc.options.keyJump, mc.options.keyShift}) {
			key.setDown(false);
		}
	}
}
