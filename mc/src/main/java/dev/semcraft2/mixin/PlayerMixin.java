package dev.semcraft2.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

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
}
