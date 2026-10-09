package dev.semcraft2.client.mixin;

import dev.semcraft2.client.PlayerSync;
import net.minecraft.client.player.LocalPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LocalPlayer.class)
abstract class LocalPlayerMixin {
	/** After the old position was saved for interpolation and before movement is sent to the server. */
	@Inject(method = "tick", at = @At("HEAD"))
	private void semcraft2$followSam(final CallbackInfo ci) {
		PlayerSync.tick((LocalPlayer) (Object) this);
	}
}
