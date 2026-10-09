package dev.semcraft2.client.mixin;

import dev.semcraft2.client.FrameExporter;
import dev.semcraft2.client.HostState;
import dev.semcraft2.client.PlayerSync;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.joml.Vector3fc;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Serious Sam's camera replaces the player's: position, rotation (with roll) and vertical field of view. */
@Mixin(Camera.class)
abstract class CameraMixin {
	private static final float DEG = (float) (Math.PI / 180.0);
	@Shadow @Final private static Vector3fc FORWARDS;
	@Shadow @Final private static Vector3fc UP;
	@Shadow @Final private static Vector3fc LEFT;
	@Shadow @Final private Vector3f forwards;
	@Shadow @Final private Vector3f up;
	@Shadow @Final private Vector3f left;
	@Shadow @Final private Quaternionf rotation;
	@Shadow private float xRot;
	@Shadow private float yRot;
	@Shadow private boolean detached;
	@Shadow private int matrixPropertiesDirty;
	@Shadow private float depthFar;

	@Shadow
	protected abstract void setPosition(double x, double y, double z);

	@Inject(method = "update", at = @At("HEAD"))
	private void semcraft2$beginFrame(final DeltaTracker deltaTracker, final CallbackInfo ci) {
		HostState.beginFrame();
		PlayerSync.frame();
	}

	@Inject(method = "alignWithEntity", at = @At("TAIL"))
	private void semcraft2$samCamera(final float partialTicks, final CallbackInfo ci) {
		HostState.Pose p = HostState.frame();
		if (p == null) {
			return;
		}

		this.xRot = p.pitch();
		this.yRot = p.yaw();
		this.rotation.rotationYXZ((float) Math.PI - p.yaw() * DEG, -p.pitch() * DEG, p.roll() * DEG);
		FORWARDS.rotate(this.rotation, this.forwards);
		UP.rotate(this.rotation, this.up);
		LEFT.rotate(this.rotation, this.left);
		this.matrixPropertiesDirty |= 3;
		this.setPosition(p.x(), p.y(), p.z());
		// first person: Steve himself is never drawn (the player model is Sam's)
		this.detached = false;
	}

	@Inject(method = "calculateFov", at = @At("HEAD"), cancellable = true)
	private void semcraft2$samFov(final float partialTicks, final CallbackInfoReturnable<Float> cir) {
		HostState.Pose p = HostState.frame();
		if (p != null) {
			cir.setReturnValue(p.vfov());
		}
	}

	@Inject(method = "update", at = @At("TAIL"))
	private void semcraft2$planes(final DeltaTracker deltaTracker, final CallbackInfo ci) {
		FrameExporter.setFar(this.depthFar);
	}
}
