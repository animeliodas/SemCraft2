package dev.semcraft2.mixin;

import dev.semcraft2.Combat;
import net.minecraft.world.level.ServerExplosion;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Minecraft's explosions: once one has pushed the player, Sam's player is pushed the same way. */
@Mixin(ServerExplosion.class)
abstract class ExplosionMixin {
	@Inject(method = "explode()I", at = @At("RETURN"))
	private void semcraft2$pushSam(final CallbackInfoReturnable<Integer> cir) {
		Combat.onExplosion((ServerExplosion) (Object) this);
	}
}
