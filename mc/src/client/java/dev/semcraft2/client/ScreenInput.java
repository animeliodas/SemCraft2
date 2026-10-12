package dev.semcraft2.client;

import com.google.gson.JsonObject;
import dev.semcraft2.SemCraft;
import dev.semcraft2.WorldBridge;
import dev.semcraft2.client.mixin.MouseHandlerAccessor;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import net.minecraft.client.gui.screens.inventory.InventoryScreen;
import net.minecraft.client.input.CharacterEvent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;

/**
 * Minecraft's screens over Sam (the inventory): Sam's window has the focus, so Sam sends its cursor (0..1 of the
 * view), clicks, keys and typed characters, and they go straight to the open screen. Opening the inventory makes the
 * player creative for as long as it's open (the creative inventory has every block and item); Minecraft decides when
 * it's closed (E, Escape, ...) and tells Sam. Client thread.
 */
final class ScreenInput {
	/** A screen Sam asked for is open (or opening). */
	private static boolean open;
	/** Waiting for the server's switch to creative to reach the client before the creative inventory can open. */
	private static int waitingTicks;
	private static double cursorX = 0.5, cursorY = 0.5;
	private static int held = -1;
	private static int modifiers;
	private static int loggedClicks;

	private ScreenInput() {
	}

	static void handle(final Minecraft mc, final JsonObject m) {
		switch (m.get("t").getAsString()) {
			case "screen" -> {
				String what = m.get("open").getAsString();
				if (what.equals("inventory") && !open && mc.player != null) {
					open = true;
					waitingTicks = 40;
					WorldBridge.inventoryMode(true);
					cursorX = 0.5;
					cursorY = 0.5;
				} else if (what.isEmpty() && open) {
					if (mc.gui.screen() != null) {
						mc.gui.screen().onClose();
					}
				}
			}
			case "cursor" -> {
				cursorX = Math.clamp(m.get("x").getAsDouble(), 0.0, 1.0);
				cursorY = Math.clamp(m.get("y").getAsDouble(), 0.0, 1.0);
				moveMouse(mc);
			}
			case "click" -> {
				Screen screen = mc.gui.screen();
				if (screen == null) {
					return;
				}

				// Sam sends 0 left, 1 right, 2 middle; Minecraft 26.3 numbers them 1 left, 3 right, 2 middle (InputConstants)
				int button = switch (m.get("b").getAsInt()) {
					case 1 -> 3;
					case 2 -> 2;
					default -> 1;
				};
				modifiers = m.has("shift") && m.get("shift").getAsBoolean() ? 1 : 0; // GLFW_MOD_SHIFT
				// through Minecraft's own mouse callback (as GLFW would call it): double clicks, drags, quick moves
				boolean down = m.get("down").getAsBoolean();
				held = down ? button : -1;
				mc.mouseHandler.onButton(mc.getWindow().handle(), new MouseButtonInfo(button, modifiers), down ? 1 : 0);
				if (loggedClicks++ < 20) {
					SemCraft.LOG.info("Sam click {} {} at {} {} on {}, carrying {}", button, down ? "down" : "up",
						String.format(java.util.Locale.ROOT, "%.1f", guiX(mc)), String.format(java.util.Locale.ROOT, "%.1f", guiY(mc)),
						screen.getClass().getSimpleName(), mc.player == null ? "" : mc.player.containerMenu.getCarried());
				}
			}
			case "char" -> {
				if (mc.gui.screen() != null) {
					mc.gui.screen().charTyped(new CharacterEvent(m.get("c").getAsInt()));
				}
			}
			case "keydown" -> {
				if (mc.gui.screen() != null) {
					mc.gui.screen().keyPressed(new KeyEvent(m.get("k").getAsInt(), 0, m.has("mods") ? m.get("mods").getAsInt() : 0));
				}
			}
			case "scrollgui" -> {
				if (mc.gui.screen() != null) {
					mc.gui.screen().mouseScrolled(guiX(mc), guiY(mc), 0.0, m.get("d").getAsDouble());
				}
			}
			default -> {
			}
		}
	}

	/** Every client tick: open the creative inventory once the client is creative; notice the screen closing. */
	static void tick(final Minecraft mc) {
		if (!open) {
			return;
		}

		if (waitingTicks > 0) {
			waitingTicks--;
			if (mc.player != null && mc.gameMode != null && mc.gameMode.getPlayerMode() == net.minecraft.world.level.GameType.CREATIVE) {
				waitingTicks = 0;
				mc.gui.setScreen(new CreativeModeInventoryScreen(mc.player, mc.player.connection.enabledFeatures(), mc.options.operatorItemsTab().get()));
				moveMouse(mc);
			} else if (waitingTicks == 0 && mc.player != null) {
				SemCraft.LOG.warn("the player didn't turn creative; opening the survival inventory");
				mc.gui.setScreen(new InventoryScreen(mc.player));
				moveMouse(mc);
			}
			return;
		}

		if (mc.gui.screen() == null) {
			open = false;
			held = -1;
			WorldBridge.inventoryMode(false);
			SemCraft.events.accept("{\"t\":\"screen\",\"open\":false}");
		}
	}

	private static double guiX(final Minecraft mc) {
		return cursorX * mc.getWindow().getGuiScaledWidth();
	}

	private static double guiY(final Minecraft mc) {
		return cursorY * mc.getWindow().getGuiScaledHeight();
	}

	private static void moveMouse(final Minecraft mc) {
		MouseHandlerAccessor mouse = (MouseHandlerAccessor) mc.mouseHandler;
		double oldX = guiX(mc), oldY = guiY(mc);
		mouse.semcraft2$setXpos(cursorX * mc.getWindow().getScreenWidth());
		mouse.semcraft2$setYpos(cursorY * mc.getWindow().getScreenHeight());
		Screen screen = mc.gui.screen();
		if (screen != null) {
			screen.mouseMoved(guiX(mc), guiY(mc));
			if (held >= 0) {
				screen.mouseDragged(new MouseButtonEvent(guiX(mc), guiY(mc), new MouseButtonInfo(held, modifiers)), guiX(mc) - oldX, guiY(mc) - oldY);
			}
		}
	}

	static boolean isOpen() {
		return open;
	}
}
