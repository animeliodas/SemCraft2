package dev.semcraft2.mixin;

import dev.semcraft2.Pushes;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * A piston moving an entity (Entity.move with MoverType.PISTON). At the head: the stand-in has no physics of its own
 * (Sam moves it), and Entity.move skips its piston limits for that, so Pushes applies them.
 */
@Mixin(Entity.class)
abstract class PistonMixin {
	@Inject(method = "move(Lnet/minecraft/world/entity/MoverType;Lnet/minecraft/world/phys/Vec3;)V", at = @At("HEAD"))
	private void semcraft2$pistonMove(final MoverType type, final Vec3 d, final CallbackInfo ci) {
		if (type == MoverType.PISTON) {
			Pushes.onPiston((Entity) (Object) this, d);
		}
	}
}
