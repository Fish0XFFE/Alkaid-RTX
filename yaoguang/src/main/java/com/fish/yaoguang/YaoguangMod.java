package com.fish.yaoguang;

import com.fish.yaoguang.client.YaoguangClientEvents;
import com.fish.yaoguang.util.VramExtender;
import net.minecraftforge.api.distmarker.Dist;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.loading.FMLEnvironment;
import net.minecraftforge.fml.loading.FMLPaths;

@Mod(YaoguangMod.MODID)
public class YaoguangMod {
    public static final String MODID = "yaoguang";

    public YaoguangMod() {
        if (FMLEnvironment.dist == Dist.CLIENT) {
            YaoguangConfig.init();
            VramExtender.setGameDir(FMLPaths.GAMEDIR.get());
            YaoguangClientEvents.register();
        }
    }
}