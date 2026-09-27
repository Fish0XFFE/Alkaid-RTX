package com.fish.yaoguang.client.gui;

import com.fish.yaoguang.YaoguangConfig;
import com.fish.yaoguang.client.YaoguangClientEvents;
import com.fish.yaoguang.util.VramExtender;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.CycleButton;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

public class YaoguangOptionsScreen extends Screen {
    private final Screen parent;
    private Button bouncesBtn, sppBtn, rayLenBtn, vramSizeBtn;
    private CycleButton<RayTracingPreset> presetBtn;
    private CycleButton<Boolean> vramToggle, denoiserBtn, ptBtn, rtBtn;

    public YaoguangOptionsScreen(Screen parent) {
        super(Component.literal("瑶光 · 光线追踪设置"));
        this.parent = parent;
    }

    @Override
    protected void init() {
        YaoguangClientEvents.ensureInitialized();

        int cx = this.width / 2;
        int w = Math.min(300, this.width - 40);
        int x = cx - w / 2;
        int y = Math.max(50, this.height / 8);
        int step = 24;

        boolean available = YaoguangClientEvents.isNativeAvailable();

        rtBtn = CycleButton.onOffBuilder(YaoguangConfig.isRayTracingEnabled())
                .create(x, y, w, 20, Component.literal("启用光线追踪"),
                        (btn, val) -> { YaoguangConfig.setRayTracingEnabled(val); updateStates(); });
        rtBtn.active = available;
        addRenderableWidget(rtBtn);
        y += step;

        presetBtn = CycleButton.<RayTracingPreset>builder(p -> Component.literal(p.getDisplayName()))
                .withValues(RayTracingPreset.values())
                .withInitialValue(YaoguangConfig.getCurrentPreset())
                .create(x, y, w, 20, Component.literal("预设"),
                        (btn, val) -> { YaoguangConfig.setCurrentPreset(val); syncFromPreset(); });
        presetBtn.active = available;
        addRenderableWidget(presetBtn);
        y += step;

        ptBtn = CycleButton.onOffBuilder(YaoguangConfig.isPathTracingEnabled())
                .create(x, y, w, 20, Component.literal("伽利略·路径追踪"),
                        (btn, val) -> { YaoguangConfig.setPathTracingEnabled(val); YaoguangConfig.markCustom(); refreshPreset(); });
        ptBtn.active = available;
        addRenderableWidget(ptBtn);
        y += step;

        bouncesBtn = new Button.Builder(Component.literal("最大反弹次数: " + YaoguangConfig.getMaxBounces()), b -> {
            int next = YaoguangConfig.getMaxBounces() + 1;
            if (next > 16) next = 1;
            YaoguangConfig.setMaxBounces(next);
            b.setMessage(Component.literal("最大反弹次数: " + next));
            YaoguangConfig.markCustom();
            refreshPreset();
        }).pos(x, y).size(w, 20).build();
        bouncesBtn.active = available;
        addRenderableWidget(bouncesBtn);
        y += step;

        sppBtn = new Button.Builder(Component.literal("每像素采样: " + YaoguangConfig.getSamplesPerPixel()), b -> {
            int next = YaoguangConfig.getSamplesPerPixel() + 1;
            if (next > 16) next = 1;
            YaoguangConfig.setSamplesPerPixel(next);
            b.setMessage(Component.literal("每像素采样: " + next));
            YaoguangConfig.markCustom();
            refreshPreset();
        }).pos(x, y).size(w, 20).build();
        sppBtn.active = available;
        addRenderableWidget(sppBtn);
        y += step;

        denoiserBtn = CycleButton.onOffBuilder(YaoguangConfig.isDenoiserEnabled())
                .create(x, y, w, 20, Component.literal("降噪器"),
                        (btn, val) -> { YaoguangConfig.setDenoiserEnabled(val); YaoguangConfig.markCustom(); refreshPreset(); });
        denoiserBtn.active = available;
        addRenderableWidget(denoiserBtn);
        y += step;

        rayLenBtn = new Button.Builder(Component.literal("光线长度: " + String.format("%.0f", YaoguangConfig.getRayLength()) + "m"), b -> {
            float next = YaoguangConfig.getRayLength() + 16f;
            if (next > 256f) next = 32f;
            YaoguangConfig.setRayLength(next);
            b.setMessage(Component.literal("光线长度: " + String.format("%.0f", next) + "m"));
            YaoguangConfig.markCustom();
            refreshPreset();
        }).pos(x, y).size(w, 20).build();
        rayLenBtn.active = available;
        addRenderableWidget(rayLenBtn);
        y += step + 8;

        vramToggle = CycleButton.onOffBuilder(YaoguangConfig.isVramExtensionEnabled())
                .create(x, y, w, 20, Component.literal("天枢·显存扩展"),
                        (btn, val) -> {
                            YaoguangConfig.setVramExtensionEnabled(val);
                            if (val) VramExtender.enableExtension(YaoguangConfig.getVramExtensionSize());
                            else VramExtender.disableExtension();
                            updateStates();
                        });
        addRenderableWidget(vramToggle);
        y += step;

        vramSizeBtn = new Button.Builder(Component.literal("扩展大小: " + YaoguangConfig.getVramExtensionSize() + " GB"), b -> {
            int next = YaoguangConfig.getVramExtensionSize() + 4;
            if (next > 32) next = 8;
            YaoguangConfig.setVramExtensionSize(next);
            if (YaoguangConfig.isVramExtensionEnabled()) {
                VramExtender.disableExtension();
                VramExtender.enableExtension(next);
            }
            b.setMessage(Component.literal("扩展大小: " + next + " GB"));
        }).pos(x, y).size(w, 20).build();
        addRenderableWidget(vramSizeBtn);
        y += step + 8;

        addRenderableWidget(new Button.Builder(Component.literal("返回"), b -> this.minecraft.setScreen(this.parent))
                .pos(x, Math.min(y, this.height - 30)).size(w, 20).build());

        updateStates();
    }

    private void syncFromPreset() {
        bouncesBtn.setMessage(Component.literal("最大反弹次数: " + YaoguangConfig.getMaxBounces()));
        sppBtn.setMessage(Component.literal("每像素采样: " + YaoguangConfig.getSamplesPerPixel()));
        rayLenBtn.setMessage(Component.literal("光线长度: " + String.format("%.0f", YaoguangConfig.getRayLength()) + "m"));
        ptBtn.setValue(YaoguangConfig.isPathTracingEnabled());
        denoiserBtn.setValue(YaoguangConfig.isDenoiserEnabled());
    }

    private void refreshPreset() {
        presetBtn.setValue(YaoguangConfig.getCurrentPreset());
    }

    private void updateStates() {
        boolean on = YaoguangConfig.isRayTracingEnabled() && YaoguangClientEvents.isNativeAvailable();
        presetBtn.active = on;
        ptBtn.active = on;
        bouncesBtn.active = on;
        sppBtn.active = on;
        denoiserBtn.active = on;
        rayLenBtn.active = on;
        vramSizeBtn.active = YaoguangConfig.isVramExtensionEnabled();
    }

    @Override
    public void render(GuiGraphics g, int mx, int my, float pt) {
        this.renderBackground(g);
        g.drawCenteredString(this.font, this.title, this.width / 2, 15, 0xFFFFFF);

        if (!YaoguangClientEvents.isNativeAvailable()) {
            g.drawCenteredString(this.font, Component.literal("§c未检测到光追硬件或不支持的驱动"),
                    this.width / 2, 32, 0xFF5555);
        } else {
            g.drawCenteredString(this.font,
                    Component.literal("§a" + YaoguangClientEvents.getDeviceName()
                            + " · " + YaoguangClientEvents.getRayTracingUnitName()),
                    this.width / 2, 32, 0x55FF55);
        }

        super.render(g, mx, my, pt);
    }
}