package dev.semcraft2;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.world.item.context.BlockPlaceContext;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.RenderShape;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.StateDefinition;
import net.minecraft.world.level.block.state.properties.IntegerProperty;
import net.minecraft.world.level.material.PushReaction;
import net.minecraft.world.phys.shapes.CollisionContext;
import net.minecraft.world.phys.shapes.VoxelShape;

/**
 * Serious Sam's level as Minecraft collision: invisible, unbreakable, and only as tall as Sam's surface inside the
 * voxel (HEIGHT in 1/16 block), so mobs, items and dropped blocks rest exactly on Sam's floors. It draws nothing (an
 * empty model, which still takes entity blob shadows) and hides nothing next to it.
 */
public final class ShellBlock extends Block {
	public static final IntegerProperty HEIGHT = IntegerProperty.create("height", 1, 16);
	public static final ResourceKey<Block> KEY = ResourceKey.create(Registries.BLOCK, Identifier.fromNamespaceAndPath(SemCraft.ID, "shell"));
	public static final ShellBlock BLOCK = new ShellBlock(Block.Properties.of()
		.strength(-1.0F, 3600000.0F)
		.noLootTable()
		.noOcclusion()
		.noTerrainParticles()
		.pushReaction(PushReaction.IMMOVEABLE)
		.isSuffocating((state, level, pos) -> false)
		.setId(KEY));
	private static final VoxelShape[] SHAPES = new VoxelShape[17];

	static {
		for (int i = 1; i <= 16; i++) {
			SHAPES[i] = Block.box(0, 0, 0, 16, i, 16);
		}
	}

	private ShellBlock(final Properties properties) {
		super(properties);
		this.registerDefaultState(this.stateDefinition.any().setValue(HEIGHT, 16));
	}

	static void register() {
		Registry.register(BuiltInRegistries.BLOCK, KEY, BLOCK);
	}

	public static BlockState withHeight(final int height) {
		return BLOCK.defaultBlockState().setValue(HEIGHT, Math.clamp(height, 1, 16));
	}

	public static boolean is(final BlockState state) {
		return state.getBlock() == BLOCK;
	}

	@Override
	protected void createBlockStateDefinition(final StateDefinition.Builder<Block, BlockState> builder) {
		builder.add(HEIGHT);
	}

	@Override
	protected VoxelShape getShape(final BlockState state, final BlockGetter level, final BlockPos pos, final CollisionContext context) {
		return SHAPES[state.getValue(HEIGHT)];
	}

	@Override
	protected RenderShape getRenderShape(final BlockState state) {
		return RenderShape.MODEL;
	}

	@Override
	protected boolean propagatesSkylightDown(final BlockState state) {
		return true;
	}

	@Override
	protected float getShadeBrightness(final BlockState state, final BlockGetter level, final BlockPos pos) {
		return 1.0F;
	}

	@Override
	protected boolean canBeReplaced(final BlockState state, final BlockPlaceContext context) {
		// Sam's floor is in the lower half of this voxel: a placed block takes the voxel and sits in the floor
		// instead of floating up to a block above it
		return state.getValue(HEIGHT) <= 8;
	}
}
