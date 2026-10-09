package dev.semcraft2.client;

import com.google.gson.JsonObject;
import dev.semcraft2.SemCraft;
import dev.semcraft2.client.mixin.KeyMappingAccessor;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.player.Inventory;

/** Input from Serious Sam (its window has the focus, so Minecraft never sees these itself). Client thread. */
final class ClientInput {
	private static int loggedKeys;

	private ClientInput() {
	}

	static void handle(final Minecraft minecraft, final JsonObject m) {
		LocalPlayer player = minecraft.player;
		switch (m.get("t").getAsString()) {
			case "key" -> {
				String k = m.get("k").getAsString();
				boolean down = !m.has("down") || m.get("down").getAsBoolean();
				KeyMapping key = switch (k) {
					case "use" -> minecraft.options.keyUse;
					case "attack" -> minecraft.options.keyAttack;
					case "pick" -> minecraft.options.keyPickItem;
					case "drop" -> minecraft.options.keyDrop;
					case "swap" -> minecraft.options.keySwapOffhand;
					default -> null;
				};
				if (key != null && down && loggedKeys < 30) {
					loggedKeys++;
					SemCraft.LOG.info("Sam key {} down; Minecraft aims at {} {}, holding {}", k,
						minecraft.hitResult == null ? "nothing" : minecraft.hitResult.getType(),
						minecraft.hitResult == null ? "" : minecraft.hitResult.getLocation(),
						player == null ? "" : player.getMainHandItem());
				}

				if (key != null) {
					if (down && !key.isDown()) {
						KeyMappingAccessor access = (KeyMappingAccessor) key;
						access.semcraft2$setClickCount(access.semcraft2$getClickCount() + 1);
					}

					key.setDown(down);
				}
			}
			case "slot" -> {
				if (player != null) {
					player.getInventory().setSelectedSlot(Math.clamp(m.get("n").getAsInt(), 0, Inventory.getSelectionSize() - 1));
				}
			}
			case "scroll" -> {
				if (player != null) {
					Inventory inventory = player.getInventory();
					int size = Inventory.getSelectionSize();
					inventory.setSelectedSlot(Math.floorMod(inventory.getSelectedSlot() + m.get("d").getAsInt(), size));
				}
			}
			case "build" -> {
				SemCraft.buildMode = m.get("on").getAsBoolean();
				if (!SemCraft.buildMode) {
					// let go of anything held in Minecraft
					minecraft.options.keyAttack.setDown(false);
					minecraft.options.keyUse.setDown(false);
				}
			}
			default -> {
			}
		}
	}
}
