package dev.semcraft2.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import dev.semcraft2.Combat;
import dev.semcraft2.LevelShell;
import dev.semcraft2.SamMonsters;
import dev.semcraft2.SemCraft;
import dev.semcraft2.WorldBridge;
import java.net.InetSocketAddress;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;

/**
 * The link to the Serious Sam plugin: a WebSocket server on 127.0.0.1 (port 25610, or -Dsemcraft2.port). Messages
 * are in docs/CONTRACT.md. Messages from any other client (test tools, the director) are handled the same way, and
 * everything Minecraft sends goes to every client.
 */
public final class HostLink extends WebSocketServer {
	private static HostLink instance;
	private static int badMessages;

	private HostLink(final int port) {
		super(new InetSocketAddress("127.0.0.1", port));
		this.setReuseAddr(true);
		this.setDaemon(true);
	}

	static void launch() {
		int port = Integer.getInteger("semcraft2.port", 25610);
		instance = new HostLink(port);
		instance.start();
		SemCraft.events = message -> instance.broadcast(message);
	}

	@Override
	public void onStart() {
		SemCraft.LOG.info("Sam link listening on 127.0.0.1:{}", this.getPort());
	}

	@Override
	public void onOpen(final WebSocket conn, final ClientHandshake handshake) {
		SemCraft.LOG.info("client connected from {}", conn.getRemoteSocketAddress());
		conn.send(String.format(Locale.ROOT, "{\"t\":\"hello\",\"v\":1,\"shm\":\"%s\",\"pid\":%d}", FrameExporter.NAME.replace("\\", "\\\\"),
			ProcessHandle.current().pid()));
	}

	@Override
	public void onClose(final WebSocket conn, final int code, final String reason, final boolean remote) {
		SemCraft.LOG.info("client disconnected ({} {})", code, reason);
	}

	@Override
	public void onMessage(final WebSocket conn, final String message) {
		try {
			JsonObject m = JsonParser.parseString(message).getAsJsonObject();
			switch (m.get("t").getAsString()) {
				case "cam" -> HostState.update(m);
				case "hello" -> SemCraft.LOG.info("Sam says hello: {}", message);
				case "level" -> {
					SemCraft.LOG.info("Sam level: {}", message);
					LevelShell.load(m.get("name").getAsString(), m.get("file").getAsString(), m.get("hash").getAsString());
				}
				case "state" -> WorldBridge.samState(m.get("hp").getAsFloat(), !m.has("alive") || m.get("alive").getAsBoolean());
				case "cmd" -> WorldBridge.command(m.get("c").getAsString());
				case "status" -> {
					conn.send(WorldBridge.status());
					Minecraft mc = Minecraft.getInstance();
					net.minecraft.world.phys.HitResult hit = mc.hitResult;
					String target = hit == null ? "null" : String.format(Locale.ROOT, "{\"type\":\"%s\",\"at\":[%.2f,%.2f,%.2f]%s}", hit.getType(),
						hit.getLocation().x, hit.getLocation().y, hit.getLocation().z,
						hit instanceof net.minecraft.world.phys.BlockHitResult b ? String.format(Locale.ROOT, ",\"block\":[%d,%d,%d],\"state\":\"%s\"",
							b.getBlockPos().getX(), b.getBlockPos().getY(), b.getBlockPos().getZ(),
							mc.level == null ? "" : mc.level.getBlockState(b.getBlockPos()).toString()) : "");
					conn.send(String.format(Locale.ROOT, "{\"t\":\"client\",\"screen\":\"%s\",\"hit\":%s,\"slot\":%d,\"held\":\"%s\",\"eye\":[%.2f,%.2f,%.2f],\"rot\":[%.1f,%.1f]}",
						mc.gui.screen() == null ? "" : mc.gui.screen().getClass().getSimpleName(), target,
						mc.player == null ? -1 : mc.player.getInventory().getSelectedSlot(),
						mc.player == null ? "" : mc.player.getMainHandItem().toString(),
						mc.player == null ? 0.0 : mc.player.getEyePosition().x, mc.player == null ? 0.0 : mc.player.getEyePosition().y,
						mc.player == null ? 0.0 : mc.player.getEyePosition().z, mc.player == null ? 0.0F : mc.player.getYRot(),
						mc.player == null ? 0.0F : mc.player.getXRot()));
				}
				case "shot" -> Combat.shot(doubles(m.getAsJsonArray("o")), doubles(m.getAsJsonArray("d")), m.get("dmg").getAsFloat(),
					m.has("n") ? m.get("n").getAsInt() : 1, m.has("spread") ? m.get("spread").getAsFloat() : 0.0F,
					m.has("range") ? m.get("range").getAsDouble() : 160.0);
				case "boom" -> Combat.boom(doubles(m.getAsJsonArray("p")), m.get("r").getAsFloat(), m.get("dmg").getAsFloat(),
					m.has("break") && m.get("break").getAsBoolean(), m.has("src") ? m.get("src").getAsString() : "");
				case "actors" -> {
					JsonArray l = m.getAsJsonArray("l");
					double[][] list = new double[l.size()][];
					for (int i = 0; i < list.length; i++) {
						list[i] = doubles(l.get(i).getAsJsonArray());
					}

					SamMonsters.update(list);
				}
				case "proj" -> {
					JsonArray l = m.getAsJsonArray("l");
					double[][] list = new double[l.size()][];
					for (int i = 0; i < list.length; i++) {
						list[i] = doubles(l.get(i).getAsJsonArray());
					}

					Combat.projectiles(list);
				}
				case "pillar" ->Combat.pillar(doubles(m.getAsJsonArray("p")), m.has("h") ? m.get("h").getAsInt() : 3);
				case "spawn" -> Combat.spawn(m.get("k").getAsString(), doubles(m.getAsJsonArray("p")), m.has("n") ? m.get("n").getAsInt() : 1,
					m.has("r") ? m.get("r").getAsDouble() : 4.0);
				default -> {
					Minecraft minecraft = Minecraft.getInstance();
					minecraft.execute(() -> ClientInput.handle(minecraft, m));
				}
			}
		} catch (RuntimeException e) {
			// a broken sender repeats itself every frame: log the first few only
			if (badMessages++ < 20) {
				SemCraft.LOG.warn("bad message {}: {}", message.length() > 200 ? message.substring(0, 200) : message, e.toString());
			}
		}
	}

	@Override
	public void onError(final WebSocket conn, final Exception e) {
		SemCraft.LOG.warn("Sam link error", e);
	}

	private static double[] doubles(final JsonArray a) {
		double[] out = new double[a.size()];
		for (int i = 0; i < out.length; i++) {
			out[i] = a.get(i).getAsDouble();
		}

		return out;
	}
}
