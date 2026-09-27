package com.fish.yaoguang.client.mixin;

import net.minecraft.client.particle.Particle;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(Particle.class)
public interface ParticleAccessor {

    @Accessor("x") double yaoguang$getX();
    @Accessor("y") double yaoguang$getY();
    @Accessor("z") double yaoguang$getZ();
    @Accessor("xo") double yaoguang$getXo();
    @Accessor("yo") double yaoguang$getYo();
    @Accessor("zo") double yaoguang$getZo();
    @Accessor("rCol") float yaoguang$getRCol();
    @Accessor("gCol") float yaoguang$getGCol();
    @Accessor("bCol") float yaoguang$getBCol();
    @Accessor("age") int yaoguang$getAge();
    @Accessor("lifetime") int yaoguang$getLifetime();
}