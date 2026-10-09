package dev.semcraft2.client.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.Hud;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Sam draws its own crosshair at the same spot, so Minecraft's would only double it. */
@Mixin(Hud.class)
abstract class HudMixin {
	@Inject(method = "extractCrosshair", at = @At("HEAD"), cancellable = true)
	private void semcraft2$noCrosshair(final GuiGraphicsExtractor graphics, final DeltaTracker deltaTracker, final CallbackInfo ci) {
		if (SemCraft.active) {
			ci.cancel();
		}
	}
}
