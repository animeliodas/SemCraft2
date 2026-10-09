package dev.semcraft2.mixin;

import dev.semcraft2.SamMonsters;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Sam monsters' proxies: hits on them go to Sam instead of doing damage here, and they neither push nor get pushed
 * (they follow their monster every update). (After universal-modder's MIT minecraft-gta5-passthrough ProxyMixin.)
 */
@Mixin(LivingEntity.class)
abstract class ProxyMixin {
	@Inject(method = "hurtServer(Lnet/minecraft/server/level/ServerLevel;Lnet/minecraft/world/damagesource/DamageSource;F)Z", at = @At("HEAD"), cancellable = true)
	private void semcraft2$proxyHurt(final ServerLevel level, final DamageSource source, final float amount, final CallbackInfoReturnable<Boolean> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		if (SamMonsters.isProxy(self)) {
			SamMonsters.onProxyHurt(self, source, amount);
			cir.setReturnValue(false);
		}
	}

	@Inject(method = "isPushable()Z", at = @At("HEAD"), cancellable = true)
	private void semcraft2$proxyNotPushable(final CallbackInfoReturnable<Boolean> cir) {
		if (SamMonsters.isProxy((LivingEntity) (Object) this)) {
			cir.setReturnValue(false);
		}
	}

	@Inject(method = "pushEntities()V", at = @At("HEAD"), cancellable = true)
	private void semcraft2$proxyNoPush(final CallbackInfo ci) {
		if (SamMonsters.isProxy((LivingEntity) (Object) this)) {
			ci.cancel();
		}
	}
}
