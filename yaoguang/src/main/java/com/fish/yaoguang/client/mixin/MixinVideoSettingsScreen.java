package com.fish.yaoguang.client.mixin;

import com.fish.yaoguang.client.gui.YaoguangOptionsScreen;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.VideoSettingsScreen;
import net.minecraft.network.chat.Component;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(VideoSettingsScreen.class)
public class MixinVideoSettingsScreen {

    @Inject(method = "init", at = @At("TAIL"), remap = true)
    private void yaoguang$addRayTracingButton(CallbackInfo ci) {
        Screen self = (Screen) (Object) this;
        ScreenInvoker invoker = (ScreenInvoker) self;

        Button btn = new Button.Builder(Component.literal("瑶光 · 光追"),
                b -> self.getMinecraft().setScreen(new YaoguangOptionsScreen(self)))
                .pos(self.width - 110, 6).size(100, 20).build();

        self.renderables.add(btn);
        invoker.yaoguang$getChildren().add(btn);
        invoker.yaoguang$getNarratables().add(btn);
    }
}