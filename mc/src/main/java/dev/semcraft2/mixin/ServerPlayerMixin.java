package dev.semcraft2.mixin;

import dev.semcraft2.SemCraft;
import dev.semcraft2.WorldBridge;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Hits on the player (mobs, explosions, arrows) land on Serious Sam's player. Vanilla works the hit out (armour,
 * invulnerability frames, difficulty) at full health, so the stand-in never dies; what it took goes to Sam, and the
 * hearts go back to Sam's health.
 */
@Mixin(ServerPlayer.class)
abstract class ServerPlayerMixin {
	@Unique
	private float semcraft2$before = -1.0F;

	@Inject(method = "hurtServer", at = @At("HEAD"))
	private void semcraft2$beforeHurt(final ServerLevel level, final DamageSource source, final float amount, final CallbackInfoReturnable<Boolean> cir) {
		ServerPlayer self = (ServerPlayer) (Object) this;
		if (SemCraft.active) {
			this.semcraft2$before = self.getHealth();
			self.setHealth(self.getMaxHealth());
		}
	}

	@Inject(method = "hurtServer", at = @At("RETURN"))
	private void semcraft2$afterHurt(final ServerLevel level, final DamageSource source, final float amount, final CallbackInfoReturnable<Boolean> cir) {
		ServerPlayer self = (ServerPlayer) (Object) this;
		if (this.semcraft2$before < 0.0F) {
			return;
		}

		float taken = self.getMaxHealth() - self.getHealth();
		self.setHealth(this.semcraft2$before);
		this.semcraft2$before = -1.0F;
		if (cir.getReturnValueZ()) {
			WorldBridge.onPlayerHurt(self, source, taken);
		}
	}

	/** A hit bigger than full health (a close creeper): Sam decides about death, the stand-in lives on. */
	@Inject(method = "die", at = @At("HEAD"), cancellable = true)
	private void semcraft2$noDeath(final DamageSource source, final CallbackInfo ci) {
		if (SemCraft.active) {
			ci.cancel();
		}
	}
}
