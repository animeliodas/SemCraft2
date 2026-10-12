package dev.semcraft2.client.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Holding the attack key mines a block only while the mouse is grabbed (Minecraft.handleKeybinds: continueAttack), and
 * Minecraft's window never has the focus here (Sam's does): in build mode, with no screen open, the mouse counts as
 * grabbed, so holding Sam's left button breaks planks and stone like holding Minecraft's.
 */
@Mixin(MouseHandler.class)
abstract class MouseHandlerMixin {
	@Inject(method = "isMouseGrabbed()Z", at = @At("HEAD"), cancellable = true)
	private void semcraft2$grabbedForSam(final CallbackInfoReturnable<Boolean> cir) {
		if (SemCraft.active && SemCraft.buildMode && Minecraft.getInstance().gui.screen() == null) {
			cir.setReturnValue(true);
		}
	}
}
