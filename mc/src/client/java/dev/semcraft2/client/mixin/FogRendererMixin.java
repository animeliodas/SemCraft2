package dev.semcraft2.client.mixin;

import dev.semcraft2.SemCraft;
import net.minecraft.client.renderer.fog.FogData;
import net.minecraft.client.renderer.fog.FogRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * No distance fog while Sam is attached (Sam has its own), and a black fog colour: the level pass clears to (fog
 * colour, alpha 0), so empty pixels stay (0, 0, 0, 0) and the world layer comes out premultiplied.
 * (From universal-modder's MIT minecraft-gta5-passthrough example.)
 */
@Mixin(FogRenderer.class)
abstract class FogRendererMixin {
	private static final float FAR_AWAY = 1.0E7F;

	@Inject(method = "updateBuffer", at = @At("HEAD"))
	private void semcraft2$noFog(final FogData fog, final CallbackInfo ci) {
		if (SemCraft.active) {
			fog.environmentalStart = FAR_AWAY;
			fog.environmentalEnd = FAR_AWAY * 2.0F;
			fog.renderDistanceStart = FAR_AWAY;
			fog.renderDistanceEnd = FAR_AWAY * 2.0F;
			fog.skyEnd = FAR_AWAY * 2.0F;
			fog.cloudEnd = FAR_AWAY * 2.0F;
			fog.color.set(0.0F, 0.0F, 0.0F, 0.0F);
		}
	}
}
