package dev.semcraft2.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.world.entity.Mob;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Minecraft's world is always noon here (so its blocks stay lit): undead don't burn in Sam's levels. */
@Mixin(Mob.class)
abstract class MobMixin {
	@Inject(method = "burnUndead()V", at = @At("HEAD"), cancellable = true)
	private void semcraft2$noSunburn(final CallbackInfo ci) {
		if (SemCraft.active) {
			ci.cancel();
		}
	}
}
