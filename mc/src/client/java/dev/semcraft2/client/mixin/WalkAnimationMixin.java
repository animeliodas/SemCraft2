package dev.semcraft2.client.mixin;

import dev.semcraft2.SemCraft;
import dev.semcraft2.client.PlayerSync;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Steve's walk (third person): the stand-in is put where Sam's player is every frame, so Minecraft sees no movement of
 * its own; his legs swing by how far Sam's player went this tick instead.
 */
@Mixin(LivingEntity.class)
abstract class WalkAnimationMixin {
	@Inject(method = "calculateEntityAnimation(Z)V", at = @At("HEAD"), cancellable = true)
	private void semcraft2$samWalk(final boolean includeHeight, final CallbackInfo ci) {
		if (SemCraft.active && (Object) this instanceof LocalPlayer player) {
			player.walkAnimation.update(Math.min(PlayerSync.tickDistance() * 4.0F, 1.0F), 0.4F, 1.0F);
			ci.cancel();
		}
	}
}
