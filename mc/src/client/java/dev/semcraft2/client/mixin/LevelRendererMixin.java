package dev.semcraft2.client.mixin;

import com.mojang.blaze3d.vertex.PoseStack;
import dev.semcraft2.ShellBlock;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.state.level.BlockOutlineRenderState;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Sam's level can be built on, but its invisible blocks get no black outline. */
@Mixin(LevelRenderer.class)
abstract class LevelRendererMixin {
	@Inject(method = "submitBlockOutline", at = @At("HEAD"), cancellable = true)
	private void semcraft2$noShellOutline(final PoseStack poseStack, final SubmitNodeCollector collector, final LevelRenderState state,
		final CallbackInfo ci) {
		BlockOutlineRenderState outline = state.blockOutlineRenderState;
		Minecraft minecraft = Minecraft.getInstance();
		if (outline != null && minecraft.level != null && ShellBlock.is(minecraft.level.getBlockState(outline.pos()))) {
			ci.cancel();
		}
	}
}
