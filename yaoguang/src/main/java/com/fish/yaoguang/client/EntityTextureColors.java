package com.fish.yaoguang.client;

import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

public final class EntityTextureColors {
    private static final Map<ResourceLocation, Entry> CACHE = new HashMap<>();

    private static final class Entry {
        final int textureId;
        final int color;

        Entry(int textureId, int color) {
            this.textureId = textureId;
            this.color = color;
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static int averageFor(Entity entity) {
        try {
            EntityRenderer renderer = Minecraft.getInstance().getEntityRenderDispatcher().getRenderer(entity);
            if (renderer instanceof LivingEntityRenderer living && entity instanceof LivingEntity target) {
                ResourceLocation location = living.getTextureLocation(target);
                return average(location);
            }
        } catch (Throwable ignored) {
        }
        return RayTracingRenderer.entityColor(entity);
    }

    public static int average(ResourceLocation location) {
        if (location == null) return 0xA0A0A0;
        try {
            AbstractTexture texture = Minecraft.getInstance().getTextureManager().getTexture(location);
            if (texture == null) return 0xA0A0A0;
            int id = texture.getId();
            Entry entry = CACHE.get(location);
            if (entry != null && entry.textureId == id) return entry.color;

            int previous = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, id);
            int width = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);
            int height = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_HEIGHT);
            int color = 0xA0A0A0;

            if (width > 0 && height > 0 && width <= 1024 && height <= 1024) {
                ByteBuffer buffer = BufferUtils.createByteBuffer(width * height * 4);
                GL11.glGetTexImage(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, buffer);
                long red = 0;
                long green = 0;
                long blue = 0;
                long count = 0;
                for (int i = 0; i < width * height; i++) {
                    int r = buffer.get(i * 4) & 0xFF;
                    int g = buffer.get(i * 4 + 1) & 0xFF;
                    int b = buffer.get(i * 4 + 2) & 0xFF;
                    int a = buffer.get(i * 4 + 3) & 0xFF;
                    if (a < 96) continue;
                    red += r;
                    green += g;
                    blue += b;
                    count++;
                }
                if (count > 0) {
                    color = ((int) (red / count) << 16) | ((int) (green / count) << 8) | (int) (blue / count);
                }
            }
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, previous);
            CACHE.put(location, new Entry(id, color));
            return color;
        } catch (Throwable t) {
            return 0xA0A0A0;
        }
    }

    private EntityTextureColors() {
    }
}
