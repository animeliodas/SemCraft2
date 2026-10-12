package dev.semcraft2.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import net.minecraft.world.level.block.state.BlockState;

/**
 * Serious Sam moves the player, so Minecraft must not collide it: the shell is only Sam's collision approximated in
 * blocks, and the player often stands a little inside it.
 */
@Mixin(Player.class)
abstract class PlayerMixin {
	@Inject(method = "tick", at = @At(value = "FIELD", target = "Lnet/minecraft/world/entity/player/Player;noPhysics:Z", opcode = Opcodes.PUTFIELD, shift = At.Shift.AFTER))
	private void semcraft2$ghost(final CallbackInfo ci) {
		if (SemCraft.active) {
			((Entity) (Object) this).noPhysics = true;
		}
	}

	/**
	 * The stand-in floats where Sam's eyes are (it flies), and Minecraft mines five times slower off the ground: Sam is
	 * standing, so the player mines as if on its feet (planks in 3 s, not 15).
	 */
	@Inject(method = "getDestroySpeed(Lnet/minecraft/world/level/block/state/BlockState;)F", at = @At("RETURN"), cancellable = true)
	private void semcraft2$minesOnFoot(final BlockState state, final CallbackInfoReturnable<Float> cir) {
		if (SemCraft.active && !((Entity) (Object) this).onGround()) {
			cir.setReturnValue(cir.getReturnValue() * 5.0F);
		}
	}
}
