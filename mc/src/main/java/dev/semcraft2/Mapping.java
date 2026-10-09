package dev.semcraft2;

/**
 * The one place Serious Sam and Minecraft coordinates meet (docs/CONTRACT.md "Units and axes").
 *
 * <p>Both games are right-handed with y up and look down -z at Sam heading 0, so positions only differ by the
 * per-level offset of the level's region in the Minecraft world. Sam never sees the offset.
 */
public final class Mapping {
	/** Offset of the current level's region (blocks). Set when Sam announces a level. */
	private static volatile double ox, oy, oz;

	private Mapping() {
	}

	public static void setOffset(final double x, final double y, final double z) {
		ox = x;
		oy = y;
		oz = z;
	}

	public static double ox() {
		return ox;
	}

	public static double oy() {
		return oy;
	}

	public static double oz() {
		return oz;
	}

	public static double mcX(final double samX) {
		return samX + ox;
	}

	public static double mcY(final double samY) {
		return samY + oy;
	}

	public static double mcZ(final double samZ) {
		return samZ + oz;
	}

	public static double samX(final double mcX) {
		return mcX - ox;
	}

	public static double samY(final double mcY) {
		return mcY - oy;
	}

	public static double samZ(final double mcZ) {
		return mcZ - oz;
	}

	/** Sam heading (0 looks down -z, positive turns left) to Minecraft yaw (0 looks down +z, 90 looks down -x). */
	public static float yaw(final float heading) {
		return wrap(180.0F - heading);
	}

	/** Sam pitch is positive looking up, Minecraft's positive looking down. */
	public static float pitch(final float samPitch) {
		return -samPitch;
	}

	public static float roll(final float samBank) {
		return samBank;
	}

	public static float heading(final float yaw) {
		return wrap(180.0F - yaw);
	}

	/** Sam hit points per Minecraft health point (Sam 100 = Minecraft 20). */
	public static final float HP_SCALE = 5.0F;

	public static float wrap(final float degrees) {
		float d = degrees % 360.0F;
		if (d >= 180.0F) {
			d -= 360.0F;
		} else if (d < -180.0F) {
			d += 360.0F;
		}

		return d;
	}
}
