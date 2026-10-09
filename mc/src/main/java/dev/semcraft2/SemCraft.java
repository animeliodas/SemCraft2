package dev.semcraft2;

import java.util.function.Consumer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SemCraft implements ModInitializer {
	public static final String ID = "semcraft2";
	public static final Logger LOG = LoggerFactory.getLogger(ID);
	/** True while Serious Sam drives the camera. The integrated server shares this JVM, so both sides read it. */
	public static volatile boolean active;
	/** Build mode: the mouse and hotbar keys act in Minecraft (the hand shows); otherwise they're Sam's. */
	public static volatile boolean buildMode;
	/** Where events for Sam go (JSON lines); the client's link sets it. */
	public static volatile Consumer<String> events = message -> {
	};

	@Override
	public void onInitialize() {
		ShellBlock.register();
		ServerLifecycleEvents.SERVER_STARTED.register(WorldBridge::attach);
		ServerLifecycleEvents.SERVER_STOPPING.register(server -> WorldBridge.detach());
		ServerTickEvents.END_SERVER_TICK.register(WorldBridge::tick);
		ServerEntityEvents.ENTITY_LOAD.register(SamMonsters::onEntityLoad);
		ServerPlayConnectionEvents.JOIN.register((handler, sender, server) -> WorldBridge.onJoin(handler.player));
		LOG.info("SemCraft 2 loaded");
	}
}
