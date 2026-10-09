package dev.semcraft2.mixin;

import dev.semcraft2.ShellBlock;
import net.minecraft.world.item.context.BlockPlaceContext;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * A block placed into a voxel of Sam's level shell (a ramp's rising slab, a step) takes the voxel: the shell is only
 * Sam's floor approximated, and it comes back when the block is broken. Clicking the shell itself still places on
 * top of it unless the shell there is low (ShellBlock.canBeReplaced).
 */
@Mixin(BlockPlaceContext.class)
abstract class BlockPlaceContextMixin {
	@Inject(method = "canPlace", at = @At("HEAD"), cancellable = true)
	private void semcraft2$intoShell(final CallbackInfoReturnable<Boolean> cir) {
		BlockPlaceContext self = (BlockPlaceContext) (Object) this;
		if (!self.replacingClickedOnBlock() && ShellBlock.is(self.getLevel().getBlockState(self.getClickedPos()))) {
			cir.setReturnValue(true);
		}
	}
}
