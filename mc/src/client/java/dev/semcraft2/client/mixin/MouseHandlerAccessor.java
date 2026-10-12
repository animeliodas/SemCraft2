package dev.semcraft2.client.mixin;

import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Sam's cursor is Minecraft's mouse while a screen is open (hover highlights read these). */
@Mixin(MouseHandler.class)
public interface MouseHandlerAccessor {
	@Accessor("xpos")
	void semcraft2$setXpos(double x);

	@Accessor("ypos")
	void semcraft2$setYpos(double y);
}
