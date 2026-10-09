package dev.semcraft2.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import dev.semcraft2.Mapping;
import dev.semcraft2.SemCraft;

/** Serious Sam's latest camera. Sam coordinates are kept to echo them with the frame; Minecraft's are derived. */
public final class HostState {
	/**
	 * @param hostFrame Sam's frame number for this camera (echoed in the exported frame)
	 * @param sx Sam camera position, sheading/spitch/sbank Sam angles (degrees), all as received
	 * @param x Minecraft camera position; yaw (0 = +z), pitch (+ = down), roll in degrees
	 * @param vfov vertical field of view (degrees), aspect width / height, w/h Sam's picture in pixels
	 * @param px Sam player's feet (Minecraft coordinates)
	 */
	public record Pose(
		long hostFrame, float sx, float sy, float sz, float sheading, float spitch, float sbank,
		double x, double y, double z, float yaw, float pitch, float roll,
		float vfov, float aspect, int w, int h, double px, double py, double pz, long receivedNanos
	) {
	}

	private static final long TIMEOUT_NANOS = 2_000_000_000L;
	private static volatile Pose latest;
	/** The pose this frame renders with, taken once per frame so every hook agrees. Render thread only. */
	private static Pose frame;

	private HostState() {
	}

	/** {"t":"cam","f":n,"p":[x,y,z],"r":[heading,pitch,bank],"vfov":deg,"aspect":a,"w":px,"h":px,"pl":[x,y,z]} (Sam coords) */
	static void update(final JsonObject m) {
		JsonArray p = m.getAsJsonArray("p");
		JsonArray r = m.getAsJsonArray("r");
		JsonArray pl = m.has("pl") ? m.getAsJsonArray("pl") : p;
		float sx = p.get(0).getAsFloat(), sy = p.get(1).getAsFloat(), sz = p.get(2).getAsFloat();
		float h = r.get(0).getAsFloat(), pi = r.get(1).getAsFloat(), b = r.size() > 2 ? r.get(2).getAsFloat() : 0.0F;
		float vfov = m.has("vfov") ? m.get("vfov").getAsFloat() : 70.0F;
		float aspect = m.has("aspect") ? m.get("aspect").getAsFloat() : 16.0F / 9.0F;
		if (!Float.isFinite(sx + sy + sz + h + pi + b + vfov + aspect) || vfov < 1.0F || vfov > 179.0F || aspect < 0.05F) {
			return;
		}

		latest = new Pose(
			m.has("f") ? m.get("f").getAsLong() : 0L, sx, sy, sz, h, pi, b,
			Mapping.mcX(sx), Mapping.mcY(sy), Mapping.mcZ(sz), Mapping.yaw(h), Mapping.pitch(pi), Mapping.roll(b),
			vfov, aspect,
			m.has("w") ? m.get("w").getAsInt() : 0, m.has("h") ? m.get("h").getAsInt() : 0,
			Mapping.mcX(pl.get(0).getAsDouble()), Mapping.mcY(pl.get(1).getAsDouble()), Mapping.mcZ(pl.get(2).getAsDouble()),
			System.nanoTime()
		);
		Pose pose = latest;
		dev.semcraft2.WorldBridge.samFeet(pose.px(), pose.py(), pose.pz());
	}

	/** The latest pose if Sam is still sending, else null. */
	static Pose live() {
		Pose p = latest;
		return p != null && System.nanoTime() - p.receivedNanos() < TIMEOUT_NANOS ? p : null;
	}

	/** The pose for the frame being rendered, or null when Sam isn't attached. */
	public static Pose frame() {
		return frame;
	}

	public static void beginFrame() {
		frame = live();
		SemCraft.active = frame != null;
	}
}
