package dev.semcraft2;

import it.unimi.dsi.fastutil.ints.IntArrayList;
import it.unimi.dsi.fastutil.longs.Long2ByteOpenHashMap;
import it.unimi.dsi.fastutil.longs.Long2ObjectOpenHashMap;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.ChunkPos;

/**
 * Turns Serious Sam's level triangles into a shell of invisible Minecraft blocks, one chunk at a time.
 *
 * <p>Each triangle is rasterised along its dominant axis on a grid finer than a block, clipped to the chunk (Sam's
 * outdoor floors are single triangles hundreds of metres wide). Each sample is pushed a hair behind its surface
 * (against the polygon's normal, which points into Sam's air) and lands in a voxel. A voxel's value is the height of
 * the highest surface point inside it, in 1/16 block (1..16), so Minecraft mobs stand exactly on Sam floors.
 * Downward-facing (ceiling) samples only fill voxels nothing else touched, as full blocks: a thin slab's underside
 * must not lift the floor on top of it.
 */
public final class Voxelizer {
	/** Grid spacing in the projection plane: at most 0.25 * sqrt(3) apart on the surface, so floors have no holes. */
	static final double SAMPLE = 0.25;
	private static final double EPS = 1.0E-3;
	/** Triangles are binned by regions of this many blocks (a power of two). */
	private static final int BIN_SHIFT = 6;
	/** Triangle flags from the .tri file. */
	public static final int FLAG_MOVING = 1;
	public static final int FLAG_PASSABLE = 2;

	private Voxelizer() {
	}

	/** Triangles in Sam coordinates: 13 floats each (v0, v1, v2, normal, flags as an int's bits). */
	public record Level(float[] tris, int count, float[] bboxMin, float[] bboxMax) {
		public int flags(final int i) {
			return Float.floatToRawIntBits(this.tris[i * 13 + 12]);
		}
	}

	/** Reads a .tri file (docs/CONTRACT.md). */
	public static Level read(final Path file) throws IOException {
		ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(file)).order(ByteOrder.LITTLE_ENDIAN);
		if (b.remaining() < 40 || b.getInt(0) != 0x31544353) {
			throw new IOException("not a SemCraft level file: " + file);
		}

		int version = b.getInt(4);
		int count = b.getInt(8);
		if (version != 1 || count < 0 || 40L + 52L * count > b.capacity()) {
			throw new IOException("bad level file header: version " + version + ", " + count + " triangles");
		}

		float[] min = {b.getFloat(16), b.getFloat(20), b.getFloat(24)};
		float[] max = {b.getFloat(28), b.getFloat(32), b.getFloat(36)};
		float[] tris = new float[count * 13];
		b.position(40);
		for (int i = 0; i < count * 13; i++) {
			tris[i] = b.getFloat();
		}

		return new Level(tris, count, min, max);
	}

	/** A level placed in Minecraft at an offset, its solid triangles binned by region for chunk queries. */
	public static final class Placed {
		final Level level;
		final double ox, oy, oz;
		/** Minecraft-space triangles: 9 doubles of vertices + 3 of unit normal each. */
		final double[] t;
		final int count;
		final Long2ObjectOpenHashMap<IntArrayList> bins = new Long2ObjectOpenHashMap<>();

		public Placed(final Level level, final double ox, final double oy, final double oz) {
			this.level = level;
			this.ox = ox;
			this.oy = oy;
			this.oz = oz;
			double[] out = new double[level.count() * 12];
			float[] s = level.tris();
			int n = 0;
			for (int i = 0; i < level.count(); i++) {
				if ((level.flags(i) & (FLAG_MOVING | FLAG_PASSABLE)) != 0) {
					continue;
				}

				int o = i * 13, d = n * 12;
				for (int k = 0; k < 3; k++) {
					out[d + k * 3] = s[o + k * 3] + ox;
					out[d + k * 3 + 1] = s[o + k * 3 + 1] + oy;
					out[d + k * 3 + 2] = s[o + k * 3 + 2] + oz;
				}

				double nx = s[o + 9], ny = s[o + 10], nz = s[o + 11];
				double nl = Math.sqrt(nx * nx + ny * ny + nz * nz);
				if (nl < 1.0E-6) {
					// no plane normal: take the winding's
					double ux = out[d + 3] - out[d], uy = out[d + 4] - out[d + 1], uz = out[d + 5] - out[d + 2];
					double vx = out[d + 6] - out[d], vy = out[d + 7] - out[d + 1], vz = out[d + 8] - out[d + 2];
					nx = uy * vz - uz * vy;
					ny = uz * vx - ux * vz;
					nz = ux * vy - uy * vx;
					nl = Math.sqrt(nx * nx + ny * ny + nz * nz);
					if (nl < 1.0E-9) {
						continue; // degenerate
					}
				}

				out[d + 9] = nx / nl;
				out[d + 10] = ny / nl;
				out[d + 11] = nz / nl;
				int bx0 = (int) Math.floor(Math.min(out[d], Math.min(out[d + 3], out[d + 6]))) >> BIN_SHIFT;
				int bx1 = (int) Math.floor(Math.max(out[d], Math.max(out[d + 3], out[d + 6]))) >> BIN_SHIFT;
				int bz0 = (int) Math.floor(Math.min(out[d + 2], Math.min(out[d + 5], out[d + 8]))) >> BIN_SHIFT;
				int bz1 = (int) Math.floor(Math.max(out[d + 2], Math.max(out[d + 5], out[d + 8]))) >> BIN_SHIFT;
				for (int bx = bx0; bx <= bx1; bx++) {
					for (int bz = bz0; bz <= bz1; bz++) {
						this.bins.computeIfAbsent(ChunkPos.pack(bx, bz), k -> new IntArrayList()).add(n);
					}
				}

				n++;
			}

			this.t = out;
			this.count = n;
		}

		public int solidTriangles() {
			return this.count;
		}

		/** Is (x, z) (Minecraft coordinates) within the level's bounds, give or take `margin` blocks? */
		public boolean around(final double x, final double z, final double margin) {
			return x >= this.level.bboxMin()[0] + this.ox - margin && x <= this.level.bboxMax()[0] + this.ox + margin
				&& z >= this.level.bboxMin()[2] + this.oz - margin && z <= this.level.bboxMax()[2] + this.oz + margin;
		}

		/** Is there any solid triangle in or near chunk (cx, cz)? */
		public boolean anyNear(final int cx, final int cz) {
			return this.bins.containsKey(ChunkPos.pack((cx << 4) >> BIN_SHIFT, (cz << 4) >> BIN_SHIFT));
		}
	}

	/** Voxelises the part of the level inside chunk (cx, cz): packed {@link BlockPos#asLong} -> height (1..16). */
	public static Long2ByteOpenHashMap voxelizeChunk(final Placed p, final int cx, final int cz) {
		Long2ByteOpenHashMap out = new Long2ByteOpenHashMap();
		LongOpenHashSet ceilings = new LongOpenHashSet();
		IntArrayList tris = p.bins.get(ChunkPos.pack((cx << 4) >> BIN_SHIFT, (cz << 4) >> BIN_SHIFT));
		if (tris == null) {
			return out;
		}

		double x0 = cx << 4, z0 = cz << 4, x1 = x0 + 16.0, z1 = z0 + 16.0;
		double[] t = p.t;
		for (int k = 0; k < tris.size(); k++) {
			int d = tris.getInt(k) * 12;
			double minX = Math.min(t[d], Math.min(t[d + 3], t[d + 6])), maxX = Math.max(t[d], Math.max(t[d + 3], t[d + 6]));
			double minZ = Math.min(t[d + 2], Math.min(t[d + 5], t[d + 8])), maxZ = Math.max(t[d + 2], Math.max(t[d + 5], t[d + 8]));
			if (maxX < x0 - EPS || minX > x1 + EPS || maxZ < z0 - EPS || minZ > z1 + EPS) {
				continue;
			}

			triangle(out, ceilings, t, d, x0, z0, x1, z1);
		}

		for (long key : ceilings) {
			out.putIfAbsent(key, (byte) 16);
		}

		return out;
	}

	/** Every chunk the level touches, voxelised (tests and small levels). */
	public static Long2ByteOpenHashMap voxelize(final Level level, final double ox, final double oy, final double oz) {
		Placed p = new Placed(level, ox, oy, oz);
		Long2ByteOpenHashMap all = new Long2ByteOpenHashMap();
		LongOpenHashSet chunks = new LongOpenHashSet();
		for (int i = 0; i < p.count; i++) {
			int d = i * 12;
			int cx0 = (int) Math.floor(Math.min(p.t[d], Math.min(p.t[d + 3], p.t[d + 6])) - 1) >> 4;
			int cx1 = (int) Math.floor(Math.max(p.t[d], Math.max(p.t[d + 3], p.t[d + 6])) + 1) >> 4;
			int cz0 = (int) Math.floor(Math.min(p.t[d + 2], Math.min(p.t[d + 5], p.t[d + 8])) - 1) >> 4;
			int cz1 = (int) Math.floor(Math.max(p.t[d + 2], Math.max(p.t[d + 5], p.t[d + 8])) + 1) >> 4;
			for (int cx = cx0; cx <= cx1; cx++) {
				for (int cz = cz0; cz <= cz1; cz++) {
					chunks.add(ChunkPos.pack(cx, cz));
				}
			}
		}

		for (long c : chunks) {
			all.putAll(voxelizeChunk(p, ChunkPos.getX(c), ChunkPos.getZ(c)));
		}

		return all;
	}

	/** Rasterise one triangle (offset d in t) along its dominant axis, keeping samples inside [x0,x1) x [z0,z1). */
	private static void triangle(final Long2ByteOpenHashMap out, final LongOpenHashSet ceilings, final double[] t, final int d,
		final double x0, final double z0, final double x1, final double z1) {
		double nx = t[d + 9], ny = t[d + 10], nz = t[d + 11];
		boolean ceiling = ny < -0.5;
		double top = Math.max(t[d + 1], Math.max(t[d + 4], t[d + 7]));
		double pd = nx * t[d] + ny * t[d + 1] + nz * t[d + 2]; // plane: n . p = pd
		double ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
		// projection axes: (iu, iv) drawn on the grid, iw solved from the plane
		int iu, iv, iw;
		if (ay >= ax && ay >= az) {
			iu = 0; iv = 2; iw = 1;
		} else if (ax >= az) {
			iu = 2; iv = 1; iw = 0;
		} else {
			iu = 0; iv = 1; iw = 2;
		}

		double[] nArr = {nx, ny, nz};
		double nw = nArr[iw];
		double u0 = t[d + iu], v0 = t[d + iv], u1 = t[d + 3 + iu], v1 = t[d + 3 + iv], u2 = t[d + 6 + iu], v2 = t[d + 6 + iv];
		double minU = Math.min(u0, Math.min(u1, u2)), maxU = Math.max(u0, Math.max(u1, u2));
		double minV = Math.min(v0, Math.min(v1, v2)), maxV = Math.max(v0, Math.max(v1, v2));
		// clip the grid to the chunk where the projection axis is x or z
		if (iu == 0) {
			minU = Math.max(minU, x0);
			maxU = Math.min(maxU, x1);
		} else if (iu == 2) {
			minU = Math.max(minU, z0);
			maxU = Math.min(maxU, z1);
		}

		if (iv == 2) {
			minV = Math.max(minV, z0);
			maxV = Math.min(maxV, z1);
		}

		double area = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0);
		double[] pt = new double[3];
		if (Math.abs(area) > 1.0E-12 && minU <= maxU && minV <= maxV) {
			double su = Math.floor(minU / SAMPLE) * SAMPLE + SAMPLE * 0.5, sv0 = Math.floor(minV / SAMPLE) * SAMPLE + SAMPLE * 0.5;
			double tol = 1.0E-9 * Math.abs(area);
			for (double u = su; u <= maxU; u += SAMPLE) {
				for (double v = sv0; v <= maxV; v += SAMPLE) {
					// edge functions (same sign as the area when inside)
					double e0 = (u1 - u0) * (v - v0) - (u - u0) * (v1 - v0);
					double e1 = (u2 - u1) * (v - v1) - (u - u1) * (v2 - v1);
					double e2 = (u0 - u2) * (v - v2) - (u - u2) * (v0 - v2);
					boolean inside = area > 0 ? e0 >= -tol && e1 >= -tol && e2 >= -tol : e0 <= tol && e1 <= tol && e2 <= tol;
					if (!inside) {
						continue;
					}

					pt[iu] = u;
					pt[iv] = v;
					pt[iw] = (pd - nArr[iu] * u - nArr[iv] * v) / nw;
					sample(out, ceilings, ceiling, top, pt, nx, ny, nz, x0, z0, x1, z1);
				}
			}
		}

		// edges and corners: thin triangles may have no grid point inside
		edge(out, ceilings, ceiling, top, t, d, d + 3, nx, ny, nz, x0, z0, x1, z1);
		edge(out, ceilings, ceiling, top, t, d + 3, d + 6, nx, ny, nz, x0, z0, x1, z1);
		edge(out, ceilings, ceiling, top, t, d + 6, d, nx, ny, nz, x0, z0, x1, z1);
	}

	/** Samples along the part of edge a-b whose x and z lie in the chunk. */
	private static void edge(final Long2ByteOpenHashMap out, final LongOpenHashSet ceilings, final boolean ceiling, final double top,
		final double[] t, final int a, final int b, final double nx, final double ny, final double nz,
		final double x0, final double z0, final double x1, final double z1) {
		double ax = t[a], ay = t[a + 1], az = t[a + 2];
		double dx = t[b] - ax, dy = t[b + 1] - ay, dz = t[b + 2] - az;
		double lo = 0.0, hi = 1.0;
		// Liang-Barsky clip against the chunk's x and z slabs
		double[][] slabs = {{dx, ax, x0, x1}, {dz, az, z0, z1}};
		for (double[] s : slabs) {
			if (Math.abs(s[0]) < 1.0E-12) {
				if (s[1] < s[2] - EPS || s[1] > s[3] + EPS) {
					return;
				}
			} else {
				double ta = (s[2] - EPS - s[1]) / s[0], tb = (s[3] + EPS - s[1]) / s[0];
				lo = Math.max(lo, Math.min(ta, tb));
				hi = Math.min(hi, Math.max(ta, tb));
			}
		}

		if (lo > hi) {
			return;
		}

		double len = Math.sqrt(dx * dx + dy * dy + dz * dz) * (hi - lo);
		int n = Math.max(1, (int) Math.ceil(len / (SAMPLE * 0.5)));
		double[] pt = new double[3];
		for (int i = 0; i <= n; i++) {
			double f = lo + (hi - lo) * i / n;
			pt[0] = ax + dx * f;
			pt[1] = ay + dy * f;
			pt[2] = az + dz * f;
			sample(out, ceilings, ceiling, top, pt, nx, ny, nz, x0, z0, x1, z1);
		}
	}

	private static void sample(final Long2ByteOpenHashMap out, final LongOpenHashSet ceilings, final boolean ceiling, final double top,
		final double[] p, final double nx, final double ny, final double nz, final double x0, final double z0, final double x1, final double z1) {
		int vx = (int) Math.floor(p[0] - nx * EPS);
		int vz = (int) Math.floor(p[2] - nz * EPS);
		if (vx < x0 || vx >= x1 || vz < z0 || vz >= z1) {
			return; // another chunk's
		}

		// a hair down as well: a wall's top edge at y = 13.0 belongs to the voxel below, not a 1/16 slab above it
		int vy = (int) Math.floor(p[1] - ny * EPS - 1.0E-4);
		long key = BlockPos.asLong(vx, vy, vz);
		if (ceiling) {
			ceilings.add(key);
			return;
		}

		int h = (int) Math.ceil((p[1] - vy) * 16.0 - 1.0E-4);
		if (top >= vy + 1 && p[1] + SAMPLE * 1.8 >= vy + 1) {
			// the surface goes on up through this voxel's top before the next sample: it fills the voxel
			h = 16;
		}

		byte height = (byte) Math.clamp(h, 1, 16);
		byte old = out.getOrDefault(key, (byte) 0);
		if (height > old) {
			out.put(key, height);
		}
	}
}
