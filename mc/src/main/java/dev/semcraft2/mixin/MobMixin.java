package dev.semcraft2.mixin;

import dev.semcraft2.SamMonsters;
import dev.semcraft2.SemCraft;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Mob.class)
abstract class MobMixin {
	/** Minecraft's world is always noon here (so its blocks stay lit): undead don't burn in Sam's levels. */
	@Inject(method = "burnUndead()V", at = @At("HEAD"), cancellable = true)
	private void semcraft2$noSunburn(final CallbackInfo ci) {
		if (SemCraft.active) {
			ci.cancel();
		}
	}

	/**
	 * Using an item on a Sam monster (a bow drawn, a block or an egg aimed at it) isn't talking to its stand-in, an
	 * invisible villager: the use goes on to the item. On both sides (the client decides whether the item gets used).
	 */
	@Inject(method = "interact(Lnet/minecraft/world/entity/player/Player;Lnet/minecraft/world/InteractionHand;Lnet/minecraft/world/phys/Vec3;)Lnet/minecraft/world/InteractionResult;",
		at = @At("HEAD"), cancellable = true)
	private void semcraft2$proxyNotInteractive(final Player player, final InteractionHand hand, final Vec3 at, final CallbackInfoReturnable<InteractionResult> cir) {
		if (SamMonsters.isProxy((Mob) (Object) this)) {
			cir.setReturnValue(InteractionResult.PASS);
		}
	}
}
