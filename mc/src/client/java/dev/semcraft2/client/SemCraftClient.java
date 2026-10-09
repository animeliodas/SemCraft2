package dev.semcraft2.client;

import dev.semcraft2.SemCraft;
import java.util.List;
import java.util.Optional;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.CloudStatus;
import net.minecraft.client.InactivityFpsLimit;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.screens.DeathScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.client.tutorial.TutorialSteps;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.HolderSet;
import net.minecraft.core.registries.Registries;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.levelgen.FlatLevelSource;
import net.minecraft.world.level.levelgen.WorldDimensions;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.flat.FlatLayerInfo;
import net.minecraft.world.level.levelgen.flat.FlatLevelGeneratorSettings;
import net.minecraft.world.level.levelgen.presets.WorldPresets;
import org.lwjgl.sdl.SDLVideo;

public class SemCraftClient implements ClientModInitializer {
	/** The world Sam plays in: an empty (void) world, so only what is built (and Sam's invisible level) is there. */
	private static final String WORLD = "semcraft2";
	private static boolean configured;
	private static boolean worldRequested;
	private static int respawnIn;
	/** Window state we set (client thread). */
	private static boolean hidden;
	private static int sizedW, sizedH;

	@Override
	public void onInitializeClient() {
		HostLink.launch();
		ClientTickEvents.END_CLIENT_TICK.register(SemCraftClient::tick);
	}

	private static void tick(final Minecraft minecraft) {
		// a death would leave the death screen over Sam's picture: respawn straight away
		if (minecraft.player != null && minecraft.gui.screen() instanceof DeathScreen && --respawnIn <= 0) {
			respawnIn = 40;
			minecraft.player.respawn();
		}

		if (!configured) {
			configured = true;
			configure(minecraft.options);
		}

		if (!worldRequested && minecraft.level == null && minecraft.gui.screen() instanceof TitleScreen && !Boolean.getBoolean("semcraft2.noAutoWorld")) {
			worldRequested = true;
			openWorld(minecraft);
		}

		window(minecraft);
	}

	/** Size Minecraft's picture to Sam's aspect, and hide the window while Sam shows it. */
	private static void window(final Minecraft minecraft) {
		HostState.Pose p = HostState.live();
		long handle = minecraft.getWindow().handle();
		boolean attached = p != null;
		boolean wantHidden = attached && !Boolean.getBoolean("semcraft2.showWindow");
		if (wantHidden != hidden) {
			hidden = wantHidden;
			if (hidden) {
				SDLVideo.SDL_HideWindow(handle);
			} else {
				SDLVideo.SDL_ShowWindow(handle);
			}

			SemCraft.LOG.info("Sam {}: Minecraft window {}", attached ? "attached" : "gone", hidden ? "hidden" : "shown");
		}

		if (!attached || p.w() <= 0 || p.h() <= 0) {
			return;
		}

		// Sam's picture, scaled down to fit the shared memory, at Sam's aspect
		double scale = Math.min(1.0, Math.min((double) FrameExporter.MAX_W / p.w(), (double) FrameExporter.MAX_H / p.h()));
		int w = (int) Math.floor(p.w() * scale) & ~1, h = (int) Math.floor(p.h() * scale) & ~1;
		if (w == sizedW && h == sizedH) {
			return;
		}

		sizedW = w;
		sizedH = h;
		// window size is in screen units; the framebuffer may be scaled (display scaling): size by their ratio
		double ratio = minecraft.getWindow().getScreenWidth() > 0 ? (double) minecraft.getWindow().getWidth() / minecraft.getWindow().getScreenWidth() : 1.0;
		int sw = (int) Math.round(w / Math.max(ratio, 0.25)), sh = (int) Math.round(h / Math.max(ratio, 0.25));
		SDLVideo.SDL_RestoreWindow(handle);
		minecraft.getWindow().setWindowed(sw, sh);
		SDLVideo.SDL_SetWindowSize(handle, sw, sh);
		SDLVideo.SDL_SyncWindow(handle);
		SemCraft.LOG.info("sized to Sam's picture: {}x{} (Sam {}x{}, window {}x{})", w, h, p.w(), p.h(), sw, sh);
	}

	/** Settings for sitting behind Sam: keep running unfocused, no sky/cloud/bobbing effects in the picture. */
	private static void configure(final Options options) {
		options.pauseOnLostFocus = false;
		options.onboardAccessibility = false;
		options.tutorialStep = TutorialSteps.NONE;
		options.cloudStatus().set(CloudStatus.OFF);
		options.bobView().set(false);
		options.vignette().set(false);
		options.improvedTransparency().set(false);
		options.inactivityFpsLimit().set(InactivityFpsLimit.MINIMIZED);
		options.fovEffectScale().set(0.0);
		options.damageTiltStrength().set(0.0);
		options.menuBackgroundBlurriness().set(0);
		options.enableVsync().set(false);
		options.framerateLimit().set(120);
		options.save();
	}

	private static void openWorld(final Minecraft minecraft) {
		if (minecraft.getLevelSource().levelExists(WORLD)) {
			SemCraft.LOG.info("opening world {}", WORLD);
			minecraft.createWorldOpenFlows().openWorld(WORLD, () -> minecraft.gui.setScreen(new TitleScreen()));
		} else {
			SemCraft.LOG.info("creating world {}", WORLD);
			LevelSettings settings = new LevelSettings("SemCraft 2", GameType.SURVIVAL, LevelSettings.DifficultySettings.DEFAULT, true,
				WorldDataConfiguration.DEFAULT);
			minecraft.createWorldOpenFlows().createFreshLevel(WORLD, settings, new WorldOptions(0L, false, false), SemCraftClient::voidWorld,
				minecraft.gui.screen());
		}
	}

	/** A flat world with a single layer of air: nothing but what gets built (Sam's level arrives as shell blocks). */
	private static WorldDimensions voidWorld(final HolderLookup.Provider registries) {
		FlatLevelGeneratorSettings flat = new FlatLevelGeneratorSettings(
			Optional.of(HolderSet.direct()), registries.lookupOrThrow(Registries.BIOME).getOrThrow(Biomes.PLAINS), List.of()
		);
		flat.getLayersInfo().add(new FlatLayerInfo(1, Blocks.AIR));
		flat.updateLayers();
		return WorldPresets.createNormalWorldDimensions(registries).replaceOverworldGenerator(registries, new FlatLevelSource(flat));
	}
}
