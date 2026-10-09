package dev.semcraft2;

import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.goal.target.NearestAttackableTargetGoal;
import net.minecraft.world.entity.npc.villager.Villager;

/** Hunt the nearest Sam monster's proxy: invisible, and often behind Sam's walls, so neither may count. */
final class ProxyTargetGoal extends NearestAttackableTargetGoal<Villager> {
	ProxyTargetGoal(final Mob mob) {
		super(mob, Villager.class, 10, false, false, (target, level) -> SamMonsters.isProxy(target));
		this.targetConditions.ignoreInvisibilityTesting().ignoreLineOfSight();
	}
}
