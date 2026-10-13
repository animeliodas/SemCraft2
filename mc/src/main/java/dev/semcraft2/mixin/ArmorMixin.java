package dev.semcraft2.mixin;

import dev.semcraft2.SamMechanics;
import dev.semcraft2.SemCraft;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * The armour bar shows Sam's armour (200 = full bar). Only on the client: the server's armour value stays the stand-in's
 * own, so Minecraft doesn't take armour off a hit Sam's armour takes off again.
 */
@Mixin(LivingEntity.class)
abstract class ArmorMixin {
	@Inject(method = "getArmorValue()I", at = @At("HEAD"), cancellable = true)
	private void semcraft2$samArmor(final CallbackInfoReturnable<Integer> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		if (SemCraft.active && self instanceof Player && self.level().isClientSide()) {
			cir.setReturnValue(Math.clamp(Math.round(SamMechanics.samArmor / 10.0F), 0, 20));
		}
	}
}
