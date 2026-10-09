package dev.semcraft2.client.mixin;

import com.mojang.blaze3d.pipeline.RenderTarget;
import dev.semcraft2.SemCraft;
import dev.semcraft2.client.FrameExporter;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * No sky while Sam is attached (Sam's sky shows through), the frame is split into world and overlay layers, and the
 * hand only shows in build mode (otherwise Sam's own weapon is in the player's hands).
 */
@Mixin(GameRenderer.class)
abstract class GameRendererMixin {
	@Shadow @Final private RenderTarget mainRenderTarget;

	@ModifyArg(
		method = "renderLevel",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/LevelRenderer;render(Lcom/mojang/blaze3d/resource/GraphicsResourceAllocator;ZLnet/minecraft/client/renderer/state/level/CameraRenderState;Lcom/mojang/renderpearl/api/buffers/GpuBufferSlice;Lorg/joml/Vector4f;ZZ)V"),
		index = 5
	)
	private boolean semcraft2$noSky(final boolean shouldRenderSky) {
		return shouldRenderSky && !SemCraft.active;
	}

	@Inject(
		method = "renderLevel",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/GameRenderer;render3dHud(Lnet/minecraft/client/renderer/state/level/CameraRenderState;Lnet/minecraft/client/renderer/state/level/PlayerRenderState;Lnet/minecraft/client/renderer/state/OptionsRenderState;Z)V")
	)
	private void semcraft2$captureWorld(final CallbackInfo ci) {
		FrameExporter.captureWorld(this.mainRenderTarget);
	}

	@Inject(method = "render", at = @At("TAIL"))
	private void semcraft2$captureOverlay(final CallbackInfo ci) {
		FrameExporter.captureOverlay(this.mainRenderTarget);
	}

	@Inject(method = "renderItemInHand", at = @At("HEAD"), cancellable = true)
	private void semcraft2$handOnlyWhenBuilding(final CallbackInfo ci) {
		if (SemCraft.active && !SemCraft.buildMode) {
			ci.cancel();
		}
	}
}
