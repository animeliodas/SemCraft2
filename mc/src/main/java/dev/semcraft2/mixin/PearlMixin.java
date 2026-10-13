package dev.semcraft2.mixin;

import dev.semcraft2.SamMechanics;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.entity.projectile.throwableitemprojectile.ThrownEnderpearl;
import net.minecraft.world.phys.HitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** An ender pearl the player threw lands: Sam's player goes there (SamMechanics.pearlLanded). */
@Mixin(ThrownEnderpearl.class)
abstract class PearlMixin {
	@Inject(method = "onHit(Lnet/minecraft/world/phys/HitResult;)V", at = @At("HEAD"))
	private void semcraft2$pearlLanded(final HitResult hit, final CallbackInfo ci) {
		Projectile self = (Projectile) (Object) this;
		SamMechanics.pearlLanded(self, self.getOwner());
	}
}
