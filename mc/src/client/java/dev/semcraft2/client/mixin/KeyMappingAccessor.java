package dev.semcraft2.client.mixin;

import net.minecraft.client.KeyMapping;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(KeyMapping.class)
public interface KeyMappingAccessor {
	@Accessor("clickCount")
	int semcraft2$getClickCount();

	@Accessor("clickCount")
	void semcraft2$setClickCount(int clickCount);
}
