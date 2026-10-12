package dev.semcraft2.client;

import dev.semcraft2.SemCraft;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/**
 * Minecraft's water and lava where Sam's player stands (the stand-in stands there): how deep, whether the eyes are under,
 * and which way it flows. Sam's player swims in it as Minecraft's would (sam/Source/Swim.cpp). Sent every client tick
 * while in a fluid, and once more on leaving it. Client thread.
 */
final class Fluids {
	private static boolean was;

	private Fluids() {
	}

	static void tick(final Minecraft mc) {
		LocalPlayer player = mc.player;
		if (player == null || mc.level == null || !SemCraft.active || HostState.live() == null) {
			was = false;
			return;
		}

		// the stand-in's fluid state, as its last tick worked it out at Sam's player's position
		double water = player.getFluidHeight(FluidTags.WATER);
		double lava = player.getFluidHeight(FluidTags.LAVA);
		boolean in = water > 0.0 || lava > 0.0;
		if (!in && !was) {
			return;
		}

		was = in;
		Vec3 flow = in ? flow(mc, player) : Vec3.ZERO;
		SemCraft.events.accept(String.format(Locale.ROOT, "{\"t\":\"fluid\",\"water\":%.3f,\"lava\":%.3f,\"eyes\":%b,\"flow\":[%.4f,%.4f,%.4f]}",
			water, lava, player.isEyeInFluid(FluidTags.WATER) || player.isEyeInFluid(FluidTags.LAVA), flow.x, flow.y, flow.z));
	}

	/** Entity.updateFluidHeightAndDoFluidPushing's direction: the fluids' flows around the body, averaged, unit length. */
	private static Vec3 flow(final Minecraft mc, final LocalPlayer player) {
		AABB box = player.getBoundingBox().deflate(0.001);
		Vec3 sum = Vec3.ZERO;
		int n = 0;
		BlockPos.MutableBlockPos p = new BlockPos.MutableBlockPos();
		for (int x = (int) Math.floor(box.minX); x < Math.ceil(box.maxX); x++) {
			for (int y = (int) Math.floor(box.minY); y < Math.ceil(box.maxY); y++) {
				for (int z = (int) Math.floor(box.minZ); z < Math.ceil(box.maxZ); z++) {
					p.set(x, y, z);
					FluidState fs = mc.level.getFluidState(p);
					if (fs.isEmpty() || y + fs.getHeight(mc.level, p) < box.minY) {
						continue;
					}

					Vec3 f = fs.getFlow(mc.level, p);
					if (f.lengthSqr() > 0.0) {
						sum = sum.add(f);
						n++;
					}
				}
			}
		}

		return n == 0 || sum.lengthSqr() < 1.0E-8 ? Vec3.ZERO : sum.normalize();
	}
}
