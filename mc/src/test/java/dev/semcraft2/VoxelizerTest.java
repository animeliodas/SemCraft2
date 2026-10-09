package dev.semcraft2;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import it.unimi.dsi.fastutil.longs.Long2ByteOpenHashMap;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.core.BlockPos;
import org.junit.jupiter.api.Test;

class VoxelizerTest {
	/** Two triangles making the quad a-b-c-d (counter-clockwise seen from where the normal points). */
	private static void quad(final List<float[]> out, final float[] a, final float[] b, final float[] c, final float[] d, final float[] n, final int flags) {
		out.add(tri(a, b, c, n, flags));
		out.add(tri(a, c, d, n, flags));
	}

	private static float[] tri(final float[] a, final float[] b, final float[] c, final float[] n, final int flags) {
		return new float[] {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], n[0], n[1], n[2], Float.intBitsToFloat(flags)};
	}

	private static Voxelizer.Level level(final List<float[]> tris) {
		float[] all = new float[tris.size() * 13];
		for (int i = 0; i < tris.size(); i++) {
			System.arraycopy(tris.get(i), 0, all, i * 13, 13);
		}

		return new Voxelizer.Level(all, tris.size(), new float[] {-100, -100, -100}, new float[] {100, 100, 100});
	}

	private static int h(final Long2ByteOpenHashMap m, final int x, final int y, final int z) {
		return m.getOrDefault(BlockPos.asLong(x, y, z), (byte) 0);
	}

	@Test
	void floorStandsAtItsHeight() {
		List<float[]> t = new ArrayList<>();
		float y = 10.3F;
		quad(t, new float[] {0, y, 0}, new float[] {0, y, 4}, new float[] {4, y, 4}, new float[] {4, y, 0}, new float[] {0, 1, 0}, 0);
		Long2ByteOpenHashMap m = Voxelizer.voxelize(level(t), 0, 0, 0);
		for (int x = 0; x < 4; x++) {
			for (int z = 0; z < 4; z++) {
				// 0.3 of a block = 4.8/16, rounded up to 5/16: mobs stand at 10.3125
				assertEquals(5, h(m, x, 10, z), "floor voxel " + x + "," + z);
				assertEquals(0, h(m, x, 9, z));
				assertEquals(0, h(m, x, 11, z));
			}
		}
	}

	@Test
	void floorOnABlockEdgeIsAFullBlockBelow() {
		List<float[]> t = new ArrayList<>();
		quad(t, new float[] {0, 10, 0}, new float[] {0, 10, 3}, new float[] {3, 10, 3}, new float[] {3, 10, 0}, new float[] {0, 1, 0}, 0);
		Long2ByteOpenHashMap m = Voxelizer.voxelize(level(t), 0, 0, 0);
		assertEquals(16, h(m, 1, 9, 1));
		assertEquals(0, h(m, 1, 10, 1));
	}

	@Test
	void wallFillsTheVoxelsBehindItsFace() {
		List<float[]> t = new ArrayList<>();
		// a wall at x = 5 facing -x (the room is at x < 5), from y 10 to 13
		quad(t, new float[] {5, 10, 0}, new float[] {5, 13, 0}, new float[] {5, 13, 2}, new float[] {5, 10, 2}, new float[] {-1, 0, 0}, 0);
		Long2ByteOpenHashMap m = Voxelizer.voxelize(level(t), 0, 0, 0);
		for (int y = 10; y <= 12; y++) {
			assertEquals(16, h(m, 5, y, 1), "wall voxel y=" + y);
			assertEquals(0, h(m, 4, y, 1), "room side stays free y=" + y);
		}

		assertEquals(0, h(m, 5, 13, 1), "no sliver on top of the wall");
	}

	@Test
	void slabUndersideDoesNotLiftTheFloorOnTop() {
		List<float[]> t = new ArrayList<>();
		// a 0.3-thick slab: top at 9.3 (up), bottom at 9.0 (down)
		quad(t, new float[] {0, 9.3F, 0}, new float[] {0, 9.3F, 2}, new float[] {2, 9.3F, 2}, new float[] {2, 9.3F, 0}, new float[] {0, 1, 0}, 0);
		quad(t, new float[] {0, 9.0F, 0}, new float[] {2, 9.0F, 0}, new float[] {2, 9.0F, 2}, new float[] {0, 9.0F, 2}, new float[] {0, -1, 0}, 0);
		Long2ByteOpenHashMap m = Voxelizer.voxelize(level(t), 0, 0, 0);
		assertEquals(5, h(m, 1, 9, 1));
		assertEquals(0, h(m, 1, 8, 1));
	}

	@Test
	void passableAndMovingPolygonsAreSkipped() {
		List<float[]> t = new ArrayList<>();
		quad(t, new float[] {0, 5, 0}, new float[] {0, 5, 2}, new float[] {2, 5, 2}, new float[] {2, 5, 0}, new float[] {0, 1, 0}, Voxelizer.FLAG_PASSABLE);
		quad(t, new float[] {0, 7, 0}, new float[] {0, 7, 2}, new float[] {2, 7, 2}, new float[] {2, 7, 0}, new float[] {0, 1, 0}, Voxelizer.FLAG_MOVING);
		assertTrue(Voxelizer.voxelize(level(t), 0, 0, 0).isEmpty());
	}

	@Test
	void offsetMovesTheShell() {
		List<float[]> t = new ArrayList<>();
		quad(t, new float[] {0, 10, 0}, new float[] {0, 10, 1}, new float[] {1, 10, 1}, new float[] {1, 10, 0}, new float[] {0, 1, 0}, 0);
		Long2ByteOpenHashMap m = Voxelizer.voxelize(level(t), 32768, 0, 0);
		assertEquals(16, h(m, 32768, 9, 0));
		assertFalse(m.containsKey(BlockPos.asLong(0, 9, 0)));
	}

	@Test
	void anglesMatchTheContract() {
		// Sam heading 0 looks down -z: Minecraft yaw 180 does too
		assertEquals(180.0F, Math.abs(Mapping.yaw(0.0F)), 1e-4);
		// Sam heading 90 looks down -x: Minecraft yaw 90
		assertEquals(90.0F, Mapping.yaw(90.0F), 1e-4);
		assertEquals(-90.0F, Mapping.yaw(-90.0F), 1e-4);
		assertEquals(-30.0F, Mapping.pitch(30.0F), 1e-4);
		for (float h = -170; h < 180; h += 37) {
			assertEquals(Mapping.wrap(h), Mapping.heading(Mapping.yaw(h)), 1e-3);
		}
	}
}
