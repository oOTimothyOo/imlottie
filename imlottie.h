/*
 * Base code written by
 * dalerank (dalerankn8@gmail.com) - code adaption, main logic
 * anatolii (anatoliigr@gmail.com) - render library fixes, pipeline redesign
 * cybercuzma2077 (cybercuzma2077@gmail.com) - porting to unreal engine
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <queue>
#include <memory>
#include <string>
#include <unordered_map>

#include "imgui.h"

#if defined(__APPLE__) && !defined(IMLOTTIE_DX11_IMPLEMENTATION)
extern "C" void *ImLottie_Metal_CreateTexture(void *device, int width, int height, const uint8_t *data);
extern "C" void ImLottie_Metal_UpdateTexture(void *texture, int width, int height, const uint8_t *data);
extern "C" void ImLottie_Metal_ReleaseTexture(void *texture);
#endif

#ifdef IMLOTTIE_DX11_IMPLEMENTATION
#	include <d3d11.h>
#endif // IMLOTTIE_DX11_IMPLEMENTATION

namespace imlottie {
	class Animation;
	std::shared_ptr<imlottie::Animation> animationLoad(const char *path);
	uint16_t animationTotalFrame(const std::shared_ptr<imlottie::Animation>& anim);
	double animationDuration(const std::shared_ptr<imlottie::Animation>& anim);
	void animationRenderSync(const std::shared_ptr<imlottie::Animation>& anim, double frameNo, uint32_t *data, int width, int height, int row_pitch);
} // namespace imlottie

namespace ImLottie {

	constexpr ImGuiID BAD_PICTUREID = ImGuiID(-1);

	// Data in system memory where saved frame
	struct NextFrame {
		std::vector<uint8_t> data;
		ImVec2 size;
	};

	// Data in system memory, this frame ready for move to tmp atlas
	struct ReadyFrame {
		ImGuiID pid = BAD_PICTUREID;
		std::vector<uint8_t> data;
		ImVec2 size;
#if DEBUG_LOTTIE_UPDATE
		const char *lottie = nullptr;
		int frame		   = 0;
		int duration_ms	   = 0;
#endif
	};

	struct LottieAnimationRenderer;



	// This code defines a struct called LottieAnim, which represents a Lottie animation. It contains various static constants,
	// such as the default size and pre-rendered frames, as well as variables that store information about the animation
	// such as the timeline and frames.
	struct LottieAnim {
		// default size for lottie renderer
		static constexpr int DEFAULT_SIZE = 32;
		// how many prerendered frames saved in array
		static constexpr int DEFAULT_PRERENDERED_FRAMES = 4;
		static constexpr int LOTTIE_SURFACE_FMT			= sizeof(uint32_t); // TEXFMT_A8R8G8B8;
		static constexpr int LOTTIE_SURFACE_FMT_BPP		= sizeof(uint32_t);

		// A unique identifier for the picture
		ImGuiID pid = BAD_PICTUREID;

#ifdef IMLOTTIE_DX11_IMPLEMENTATION
		// DirectX 11 implementation
		ID3D11Texture2D *texture	  = nullptr;
		ID3D11ShaderResourceView *srv = nullptr;
#endif // IMLOTTIE_DX11_IMPLEMENTATION
		struct {
			int width  = DEFAULT_SIZE;
			int height = DEFAULT_SIZE;
		} canvas;

		struct {
			double duration_ms	  = 0.0;
			double frame_ms		  = 0.0;
			double last_ms		  = 0.0;
			double last_output_ms = 0.0;
		} timeline;

		struct {
			uint16_t current = 0;
			uint16_t total	 = 0;
		} frame;

		double playhead	   = 0.0;
		double frameCursor = 0.0;
		float speed		   = 1.0f;
		bool smooth		   = true;

		// Flags for the animation
		bool loop		= false;
		bool play		= false;
		bool renderonce = false;

		int maxPrerenderedFrames = DEFAULT_PRERENDERED_FRAMES;
		std::string lottiePath;

		std::shared_ptr<imlottie::Animation> anim;
		// we need save future frames, because are can have
		// different time for render, thread render it on loop
		std::queue<NextFrame> prerenderedFrames;

		// base frame data for blending/output
		NextFrame baseFrame;

		// here saved frame, which need for display, on every render()
		// call prerendered frame will moved here when time for next frame gone
		ReadyFrame currentFrame;

		// Grabs the current frame and stores it in the "f" parameter
		bool grabCurrentFrame(ReadyFrame& f) {
			if (currentFrame.pid == BAD_PICTUREID) {
				return false;
			}

			std::swap(f, currentFrame);
			return true;
		}

		// Returns a hash code based on the properties of the Lottie animation
		static ImGuiID getPropsHash(const char *lottie, const int canvasWidth, const int canvasHeight, bool loop) {
			char hash[512];
			snprintf(hash, 511, "lottie:%s|canvasHeight:%d|canvasWidth:%d|loop:%d", lottie, canvasWidth, canvasHeight, loop);
			return ImHashStr(hash, 0, 0xc001f00d);
			;
		}

		// Loads the Lottie animation from the specified file path
		bool load(const char *path, int w, int h, bool _loop, bool _play, int _prerenderedFrames, int rate, ImGuiID _pid) {
			if (!path || 0 == *path) {
				return false;
			}

			canvas.width  = std::max<int>(w, DEFAULT_SIZE);
			canvas.height = std::max<int>(h, DEFAULT_SIZE);

			loop				 = _loop;
			play				 = _play;
			pid					 = _pid;
			maxPrerenderedFrames = std::max<int>(_prerenderedFrames, DEFAULT_PRERENDERED_FRAMES);

			while (!prerenderedFrames.empty()) {
				prerenderedFrames.pop();
			}
			baseFrame				= {};
			currentFrame			= {};
			frame.current			= 0;
			playhead				= 0.0;
			frameCursor				= 0.0;
			timeline.last_ms		= 0;
			timeline.last_output_ms = 0;

			lottiePath = path;
			anim	   = imlottie::animationLoad(path);

			if (anim) {
				int customRate = rate;
				frame.total	   = (uint16_t)imlottie::animationTotalFrame(anim);
				if (frame.total == 0) {
					return false;
				}
				double oneFrameMs = (imlottie::animationDuration(anim) * 1000.0) / frame.total;
				timeline.frame_ms = std::max(0.001, oneFrameMs);
				updateRate(customRate);
			} else {
				printf("Lottie::animation load failed from <%s>", path);
				return false;
			}

			return true;
		}

		void updateRate(int rate) {
			const double base_ms   = timeline.frame_ms > 0.0 ? timeline.frame_ms : 1.0;
			const double output_ms = rate > 0 ? (1000.0 / rate) : base_ms;
			const double smooth_ms = 1000.0 / 60.0;
			double capped_ms	   = output_ms;
			if (smooth && capped_ms > smooth_ms) {
				capped_ms = smooth_ms;
			}
			timeline.duration_ms	= std::max(0.001, capped_ms);
			timeline.last_output_ms = 0.0;
		}

		void updateSpeed(float value) {
			speed = std::max(0.0f, value);
		}

		static void blendFrames(const NextFrame& base, const NextFrame& next, float t, std::vector<uint8_t>& out) {
			if (base.data.size() != next.data.size()) {
				out = base.data;
				return;
			}

			if (t < 0.0f) {
				t = 0.0f;
			} else if (t > 1.0f) {
				t = 1.0f;
			}
			const uint32_t alpha = static_cast<uint32_t>(t * 256.0f);
			const uint32_t inv	 = 256 - alpha;
			out.resize(base.data.size());

			const uint8_t *a = base.data.data();
			const uint8_t *b = next.data.data();
			uint8_t *dst	 = out.data();

			for (size_t i = 0; i < base.data.size(); ++i) {
				dst[i] = static_cast<uint8_t>((a[i] * inv + b[i] * alpha) >> 8);
			}
		}

		bool renderFrame(double frameIndex, NextFrame& out) {
			if (!anim || frame.total == 0 || frameIndex < 0.0 || frameIndex >= static_cast<double>(frame.total)) {
				return false;
			}

			const size_t bufferSize = canvas.width * canvas.height * LOTTIE_SURFACE_FMT_BPP;
			out.data.resize(bufferSize);
			out.size = ImVec2(static_cast<float>(canvas.width), static_cast<float>(canvas.height));
			imlottie::animationRenderSync(anim, frameIndex, reinterpret_cast<uint32_t *>(out.data.data()), canvas.width, canvas.height, canvas.width * LOTTIE_SURFACE_FMT_BPP);
			return true;
		}

		bool advanceFrame() {
			if (frame.total == 0) {
				return false;
			}

			if (!loop && frame.current >= frame.total) {
				return false;
			}

			NextFrame nextFrame;
			if (!prerenderedFrames.empty()) {
				std::swap(nextFrame, prerenderedFrames.front());
				prerenderedFrames.pop();
			} else if (!renderFrame(frame.current, nextFrame)) {
				return false;
			}

			baseFrame = std::move(nextFrame);
			frame.current++;
			if (loop) {
				frame.current %= frame.total;
			}
			return true;
		}

		void fillPrerenderedFrames() {
			const size_t max_prerendered = static_cast<size_t>(std::max<int>(maxPrerenderedFrames, DEFAULT_PRERENDERED_FRAMES));

			while (prerenderedFrames.size() < max_prerendered) {
				uint16_t nextFrameIndex = frame.current + static_cast<uint16_t>(prerenderedFrames.size());
				if (loop && frame.total > 0) {
					nextFrameIndex = nextFrameIndex % frame.total;
				}

				if (!loop && nextFrameIndex >= frame.total) {
					break;
				}

				NextFrame nextFrame;
				if (!renderFrame(nextFrameIndex, nextFrame)) {
					break;
				}
				prerenderedFrames.push(std::move(nextFrame));
			}
		}

		bool render(uint32_t curTime) {
			if (pid == BAD_PICTUREID || !(play || renderonce)) {
				return false;
			}

			renderonce = false;

			if (!loop && frame.current > frame.total) {
				return false;
			}

			if (frame.total == 0 || !anim) {
				return false;
			}

			const double curTimeMs = static_cast<double>(curTime);
			if (timeline.last_ms == 0.0) {
				timeline.last_ms		= curTimeMs;
				timeline.last_output_ms = curTimeMs - timeline.duration_ms;
			}

			const double delta = curTimeMs - timeline.last_ms;
			timeline.last_ms   = curTimeMs;

			const double frame_ms = timeline.frame_ms > 0.0 ? timeline.frame_ms : 1.0;
			if (speed > 0.0f && delta > 0.0) {
				if (smooth) {
					playhead += (static_cast<double>(delta) / frame_ms) * speed;
				} else {
					frameCursor += (static_cast<double>(delta) / frame_ms) * speed;
				}
			}

			if (smooth) {
				if (loop && frame.total > 0) {
					playhead = std::fmod(playhead, static_cast<double>(frame.total));
					if (playhead < 0.0) {
						playhead += static_cast<double>(frame.total);
					}
				} else {
					const double maxFrame = std::max(0.0, static_cast<double>(frame.total) - 1.0);
					if (playhead < 0.0) {
						playhead = 0.0;
					} else if (playhead > maxFrame) {
						playhead = maxFrame;
					}
				}

				if (curTimeMs <= timeline.last_output_ms) {
					return false;
				}
				timeline.last_output_ms = curTimeMs;

				frame.current = static_cast<uint16_t>(std::floor(playhead));

				NextFrame nextFrame;
				if (!renderFrame(playhead, nextFrame)) {
					return false;
				}

				currentFrame.data = std::move(nextFrame.data);
				currentFrame.size = nextFrame.size;
				currentFrame.pid  = pid;
#if DEBUG_LOTTIE_UPDATE
				currentFrame.lottie		 = lottiePath.c_str();
				currentFrame.frame		 = static_cast<uint16_t>(std::floor(playhead));
				currentFrame.duration_ms = timeline.duration_ms;
#endif // DEBUG_LOTTIE_UPDATE
				return true;
			}

			// non-smooth, integer frame path
			while (frameCursor >= 1.0) {
				if (!advanceFrame()) {
					frameCursor = 0.0;
					break;
				}
				frameCursor -= 1.0;
			}

			fillPrerenderedFrames();

			if (baseFrame.data.empty()) {
				if (!advanceFrame()) {
					return false;
				}
				fillPrerenderedFrames();
			}

			const double elapsed = curTimeMs - timeline.last_output_ms;
			if (elapsed < timeline.duration_ms) {
				return false;
			}

			if (timeline.duration_ms > 0.0) {
				timeline.last_output_ms += std::floor(elapsed / timeline.duration_ms) * timeline.duration_ms;
			} else {
				timeline.last_output_ms = curTimeMs;
			}

			currentFrame.data = baseFrame.data;
			currentFrame.size = baseFrame.size;
			currentFrame.pid  = pid;
#if DEBUG_LOTTIE_UPDATE
			currentFrame.lottie		 = lottiePath.c_str();
			currentFrame.frame		 = frame.current;
			currentFrame.duration_ms = timeline.duration_ms;
#endif // DEBUG_LOTTIE_UPDATE
			return true;
		}

		// Simple helper function to load an image into a DX11 texture with common settings
#ifdef IMLOTTIE_DX11_IMPLEMENTATION
		bool createTextureFromData(uint8_t *image_data, ::ID3D11Device *pd3dDevice) {
			if (image_data == NULL) {
				return false;
			}

			// Create texture
			D3D11_TEXTURE2D_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.Width			  = canvas.width;
			desc.Height			  = canvas.height;
			desc.MipLevels		  = 1;
			desc.ArraySize		  = 1;
			desc.Format			  = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage			  = D3D11_USAGE_DEFAULT;
			desc.BindFlags		  = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags	  = D3D11_CPU_ACCESS_WRITE;
			desc.Usage			  = D3D11_USAGE_DYNAMIC;

			D3D11_SUBRESOURCE_DATA subResource;
			subResource.pSysMem			 = image_data;
			subResource.SysMemPitch		 = desc.Width * 4;
			subResource.SysMemSlicePitch = 0;
			pd3dDevice->CreateTexture2D(&desc, &subResource, &texture);

			// Create texture view
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format					  = DXGI_FORMAT_B8G8R8A8_UNORM;
			srvDesc.ViewDimension			  = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels		  = desc.MipLevels;
			srvDesc.Texture2D.MostDetailedMip = 0;
			pd3dDevice->CreateShaderResourceView(texture, &srvDesc, &srv);
			// pTexture->Release();
			return true;
		}

		bool updateTextureFromData(unsigned char *image_data, ID3D11DeviceContext *ctx) {
			if (!image_data) {
				return false;
			}

			D3D11_MAPPED_SUBRESOURCE ms;
			HRESULT hr = ctx->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
			if (FAILED(hr)) {
				return false;
			}

			const uint8_t *src = image_data;
			uint8_t *dst	   = (uint8_t *)ms.pData;

			uint32_t bytes_per_row = canvas.width * 4;
			for (int y = 0; y < canvas.height; ++y) {
				memcpy(dst, src, bytes_per_row);
				src += bytes_per_row;
				dst += ms.RowPitch;
			}

			ctx->Unmap(texture, 0);
			return true;
		}
#endif // IMLOTTIE_DX11_IMPLEMENTATION
	};

	struct LottieRenderCommand {
		enum Type { UNKNOWN = 0, ADD_CONFIG, DISCARD_PID, SETUP_PID, SETUP_PLAY, SETUP_RENDER, SETUP_RATE, SETUP_SPEED };

		Type type;
		std::string path;
		int w, h;
		int loop;
		int rate;
		float speed;
		ImGuiID pid;
		bool play;
		bool render;
	};

	// this thread resolve command to load lotti animations, and their render frames
	struct LottieRenderThread {
		std::atomic_int terminating = false;

		bool popCommand(LottieRenderCommand& command) {
			std::lock_guard<std::mutex> lock(commandsMutex);
			if (commands.empty())
				return false;

			std::swap(command, commands.front());
			commands.pop();
			return true;
		}

		void addCommand(const LottieRenderCommand& command) {
			std::lock_guard<std::mutex> lock(commandsMutex);
			if (commands.size() > 100) {
				commands.pop();
			}
			commands.push(command);
		}

		std::thread independentThread;
		std::unordered_map<uint32_t, LottieAnim> animations;

		// this queue contain commands for animations
		// load - load animation may take much time
		// discard - after reset\remove image in PM we need remove it from quese
		//           because PM cant contain real image for it
		// setup pid - animation need assign PictureID in PM, this pid
		//             received later than load, when we have area in atlas for image
		// setup play flag - for future, when we need change play status
		std::mutex commandsMutex;
		std::queue<LottieRenderCommand> commands;

		// this queue contain ready frames from animations, it placed in system
		// memory that another thread can copy their to PM texture later
		std::mutex readyFramesMutex;
		std::queue<ReadyFrame> readyFrames;
		std::atomic<uint32_t> curtime_ms{0};

		void pushReadyFrame(ReadyFrame& frame, size_t maxAnimSize) {
			std::lock_guard<std::mutex> lock(readyFramesMutex);
			// remove extra frames, that avoid creating infinite queue
			if (readyFrames.size() > maxAnimSize)
				readyFrames.pop();

			readyFrames.push({});
			std::swap(readyFrames.back(), frame);
		}

		bool popReadyFrame(ReadyFrame& frame) {
			std::lock_guard<std::mutex> lock(readyFramesMutex);
			if (readyFrames.empty())
				return false;

			std::swap(frame, readyFrames.front());
			readyFrames.pop();
			return true;
		}

		// resolve command in thread, because it can be added async from another thread
		void resolveCommand(const LottieRenderCommand& cmd) {
			switch (cmd.type) {
			case LottieRenderCommand::ADD_CONFIG: {
				LottieAnim anim;
				bool loadOk = anim.load(cmd.path.c_str(), cmd.w, cmd.h, cmd.loop, true, 2, cmd.rate, cmd.pid);
				if (loadOk) {
					anim.updateSpeed(cmd.speed);
					anim.updateRate(cmd.rate);
					animations.insert({cmd.pid, std::move(anim)});
					auto it = animations.find(cmd.pid);
					if (it != animations.end()) {
						it->second.advanceFrame();
						if (!it->second.baseFrame.data.empty()) {
							ReadyFrame initial;
							initial.pid	 = it->second.pid;
							initial.data = it->second.baseFrame.data;
							initial.size = it->second.baseFrame.size;
							pushReadyFrame(initial, animations.size() * 2);
						}
					}
				}
			} break;

			case LottieRenderCommand::DISCARD_PID: {
				auto it = std::find_if(animations.begin(), animations.end(), [pid = cmd.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (it != animations.end())
					animations.erase(it);
			} break;

			case LottieRenderCommand::SETUP_PID: {
				const uint32_t propsHash = LottieAnim::getPropsHash(cmd.path.c_str(), cmd.w, cmd.h, cmd.loop);
				auto it					 = animations.find(propsHash);
				if (it != animations.end()) {
					it->second.pid = cmd.pid;
				}
			} break;

			case LottieRenderCommand::SETUP_PLAY: {
				auto it = std::find_if(animations.begin(), animations.end(), [pid = cmd.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (it != animations.end()) {
					it->second.play = cmd.play;
				}
			} break;

			case LottieRenderCommand::SETUP_RENDER: {
				auto it = std::find_if(animations.begin(), animations.end(), [pid = cmd.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (it != animations.end()) {
					it->second.renderonce = cmd.render;
				}
			} break;

			case LottieRenderCommand::SETUP_RATE: {
				auto it = std::find_if(animations.begin(), animations.end(), [pid = cmd.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (it != animations.end()) {
					it->second.updateRate(cmd.rate);
				}
			} break;

			case LottieRenderCommand::SETUP_SPEED: {
				auto it = std::find_if(animations.begin(), animations.end(), [pid = cmd.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (it != animations.end()) {
					it->second.updateSpeed(cmd.speed);
				}
			} break;

			default: break;
			}
		}

		void execute() {
			while (!terminating.load()) {
				bool didWork = false;
				LottieRenderCommand cmd;
				if (popCommand(cmd)) {
					resolveCommand(cmd);
					didWork = true;
				}

				if (animations.empty()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}

				// render animations and extract current animation frame to ready frames array
				const size_t maxAnimSize = animations.size() * 2;
				for (auto& anim : animations) {
					// it's loop here for all animations and frame render make a time, break
					// it when thread want stop
					if (terminating.load())
						return;

					// prerender next frames and prepare copy data to current frame if need
					const uint32_t now = curtime_ms.load(std::memory_order_relaxed);
					if (anim.second.render(now)) {
						didWork = true;
					}

					// if current frame ready, we need copy it to ready frames array
					// ready frames array will be copied to dynatlas on frame update from
					// main thread so we need use mutex for guard access when array changes
					ReadyFrame currentFrame;
					if (anim.second.grabCurrentFrame(currentFrame)) {
						pushReadyFrame(currentFrame, maxAnimSize);
						didWork = true;
					}
				}

				if (!didWork) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}
	};

	// mininmal info about lottie aninmation, need
	// for fast check we pid assigned for any animation
	struct LottieAnimDesc {
		ImVec2 size;
		void *srv	= nullptr;
		ImGuiID pid = BAD_PICTUREID;
		int rate	= 0;
		float speed = 1.0f;
	};

	struct LottieAnimationRenderer {
		LottieRenderThread renderThread;

		std::mutex animationsPresentMutex;
		std::unordered_map<ImGuiID, LottieAnimDesc> animationsPresent;

#ifndef IMLOTTIE_DX11_IMPLEMENTATION
		// Metal backend texture cache
		std::unordered_map<ImGuiID, std::vector<uint8_t>> metalFrameCache;
		std::unordered_map<ImGuiID, ImVec2> metalFrameSizes;
		std::unordered_map<ImGuiID, void *> metalTextures; // MTLTexture pointers
		std::mutex metalFrameCacheMutex;
		std::mutex metalTextureMutex;

		static void unpremultiply_bgra(std::vector<uint8_t>& data) noexcept {
			if (data.empty()) {
				return;
			}

			for (size_t i = 0; i + 3 < data.size(); i += 4) {
				const uint8_t a = data[i + 3];
				if (a == 0) {
					data[i]		= 0;
					data[i + 1] = 0;
					data[i + 2] = 0;
					continue;
				}

				data[i]		= static_cast<uint8_t>((static_cast<unsigned int>(data[i]) * 255U + a / 2U) / a);
				data[i + 1] = static_cast<uint8_t>((static_cast<unsigned int>(data[i + 1]) * 255U + a / 2U) / a);
				data[i + 2] = static_cast<uint8_t>((static_cast<unsigned int>(data[i + 2]) * 255U + a / 2U) / a);
			}
		}

		// Helper to get cached frame data
		const std::vector<uint8_t> *getCachedFrame(ImGuiID pid) const {
			auto it = metalFrameCache.find(pid);
			return (it != metalFrameCache.end()) ? &it->second : nullptr;
		}

		// Helper to get frame size
		ImVec2 getCachedFrameSize(ImGuiID pid) const {
			auto it = metalFrameSizes.find(pid);
			return (it != metalFrameSizes.end()) ? it->second : ImVec2(0, 0);
		}

		// Helper to get Metal texture
		void *getMetalTexture(ImGuiID pid) const {
			auto it = metalTextures.find(pid);
			return (it != metalTextures.end()) ? it->second : nullptr;
		}

		// Cache Metal texture
		void setMetalTexture(ImGuiID pid, void *texture) {
			std::lock_guard<std::mutex> lock(metalTextureMutex);
			metalTextures[pid] = texture;
		}
#endif

		ImGuiID match(const char *path, int w, int h, bool loop, int rate, float speed) {
			if (!path || 0 == *path) {
				return false;
			}

			std::lock_guard<std::mutex> lock(animationsPresentMutex);
			ImGuiID propsHash = LottieAnim::getPropsHash(path, w, h, loop);
			auto it			  = animationsPresent.find(propsHash);
			if (it == animationsPresent.end()) {
				ImVec2 prefferedSize;
				prefferedSize.x = (float)std::max<int>(w, LottieAnim::DEFAULT_SIZE);
				prefferedSize.y = (float)std::max<int>(h, LottieAnim::DEFAULT_SIZE);
				LottieAnimDesc animDesc;
				animDesc.pid   = propsHash;
				animDesc.size  = prefferedSize;
				animDesc.rate  = rate;
				animDesc.speed = speed;
				animationsPresent.insert({propsHash, animDesc});

				LottieRenderCommand command;
				command.type  = LottieRenderCommand::ADD_CONFIG;
				command.path  = path;
				command.w	  = (int)prefferedSize.x;
				command.h	  = (int)prefferedSize.y;
				command.loop  = loop;
				command.rate  = rate;
				command.speed = speed;
				command.pid	  = propsHash;
				renderThread.addCommand(command);
				return propsHash;
			}

			if (it->second.rate != rate) {
				it->second.rate = rate;
				LottieRenderCommand command;
				command.type = LottieRenderCommand::SETUP_RATE;
				command.pid	 = propsHash;
				command.rate = rate;
				renderThread.addCommand(command);
			}

			if (it->second.speed != speed) {
				it->second.speed = speed;
				LottieRenderCommand command;
				command.type  = LottieRenderCommand::SETUP_SPEED;
				command.pid	  = propsHash;
				command.speed = speed;
				renderThread.addCommand(command);
			}

			return propsHash;
		}

		bool render(ImGuiID pid) {
			LottieRenderCommand command;
			command.type   = LottieRenderCommand::SETUP_RENDER;
			command.pid	   = pid;
			command.render = true;
			renderThread.addCommand(command);
			return true;
		}

		void *image(ImGuiID pid) {
			// std::lock_guard<std::mutex> lock(animationsPresentMutex);
			auto it = std::find_if(animationsPresent.begin(), animationsPresent.end(), [pid](auto& a) {
				return a.second.pid == pid;
			});
			return (it == animationsPresent.end()) ? nullptr : it->second.srv;
		}

		void play(ImGuiID pid, bool play) {
			LottieRenderCommand command;
			command.type = LottieRenderCommand::SETUP_PLAY;
			command.pid	 = pid;
			command.play = play;
			renderThread.addCommand(command);
		}

		void discard(ImGuiID pid) {
			LottieRenderCommand command;
			command.type = LottieRenderCommand::DISCARD_PID;
			command.pid	 = pid;
			renderThread.addCommand(command);

			std::lock_guard<std::mutex> lock(animationsPresentMutex);
			auto it = std::find_if(animationsPresent.begin(), animationsPresent.end(), [pid](auto& a) {
				return a.second.pid == pid;
			});
			if (it != animationsPresent.end()) {
				animationsPresent.erase(it);
			}
		}

#ifdef IMLOTTIE_DX11_IMPLEMENTATION
		void uploadReadyFramesToSysTex(ID3D11Device *pd3dDevice, ID3D11DeviceContext *ctx) {
			// prepared frames stored in readyFrames array now (in system memory)
			// but need move those to gpu memory with textures
			ReadyFrame readyFrame;
			while (renderThread.popReadyFrame(readyFrame)) {
				const auto& it = renderThread.animations.find(readyFrame.pid);

				if (!it->second.texture) {
					it->second.createTextureFromData(readyFrame.data.data(), pd3dDevice);
					auto rit = std::find_if(animationsPresent.begin(), animationsPresent.end(), [pid = it->second.pid](auto& a) {
						return a.second.pid == pid;
					});
					if (rit != animationsPresent.end())
						rit->second.srv = it->second.srv;
					break;
				} else {
					it->second.updateTextureFromData(readyFrame.data.data(), ctx);
				}
			}

			renderThread.curtime_ms.store(static_cast<uint32_t>(ImGui::GetTime() * 1000.0f), std::memory_order_relaxed);
		}
#else
		// Metal backend: create Metal textures from frame data
		void uploadReadyFramesToSysTex() {
			uploadReadyFramesToSysTex(nullptr);
		}

		void uploadReadyFramesToSysTex(void *device) {
#	if defined(__APPLE__)
			ReadyFrame readyFrame;
			while (renderThread.popReadyFrame(readyFrame)) {
				if (!device) {
					continue;
				}

				// Cache frame data and size
				unpremultiply_bgra(readyFrame.data);
				{
					std::lock_guard<std::mutex> lock(metalFrameCacheMutex);
					metalFrameCache[readyFrame.pid] = readyFrame.data;
					metalFrameSizes[readyFrame.pid] = readyFrame.size;
				}

				void *texture	 = getMetalTexture(readyFrame.pid);
				const int width	 = static_cast<int>(readyFrame.size.x);
				const int height = static_cast<int>(readyFrame.size.y);
				if (!texture) {
					texture = ImLottie_Metal_CreateTexture(device, width, height, readyFrame.data.data());
					if (!texture) {
						continue;
					}
					setMetalTexture(readyFrame.pid, texture);
				} else {
					ImLottie_Metal_UpdateTexture(texture, width, height, readyFrame.data.data());
				}

				auto rit = std::find_if(animationsPresent.begin(), animationsPresent.end(), [pid = readyFrame.pid](auto& a) {
					return a.second.pid == pid;
				});
				if (rit != animationsPresent.end()) {
					rit->second.srv = texture;
				}
			}
			renderThread.curtime_ms.store(static_cast<uint32_t>(ImGui::GetTime() * 1000.0f), std::memory_order_relaxed);
#	else
			(void)device;
			ReadyFrame readyFrame;
			while (renderThread.popReadyFrame(readyFrame)) {
			}
			renderThread.curtime_ms.store(static_cast<uint32_t>(ImGui::GetTime() * 1000.0f), std::memory_order_relaxed);
#	endif
		}
#endif // IMLOTTIE_DX11_IMPLEMENTATION

		LottieAnimationRenderer() {
			renderThread.independentThread = std::thread([this]() {
				renderThread.execute();
			});
		}

		~LottieAnimationRenderer() {
			renderThread.terminating.store(true);
			if (renderThread.independentThread.joinable()) {
				renderThread.independentThread.join();
			}
#if !defined(IMLOTTIE_DX11_IMPLEMENTATION) && defined(__APPLE__)
			for (auto& entry : metalTextures) {
				ImLottie_Metal_ReleaseTexture(entry.second);
			}
#endif
		}
	};

	namespace detail {
		inline std::unique_ptr<LottieAnimationRenderer> g_lottieRenderer;
	}

	void LottieAnimation(const char *path, const ImVec2& size, bool loop, int rate, float speed);

	inline void LottieAnimation(const char *path, const ImVec2& size, bool loop, int rate) {
		LottieAnimation(path, size, loop, rate, 1.0f);
	}

	inline void LottieAnimation(const char *path, const ImVec2& size, bool loop, int rate, float speed) {
		ImVec2 pos, centre;
		ImGuiWindow *window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;

		ImGuiContext& g			= *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id		= window->GetID(path);
		static std::unordered_map<ImGuiID, void *> lastTextureByWidget;
		static std::unordered_map<ImGuiID, ImGuiID> lastPidByWidget;
		static std::unordered_map<ImGuiID, int> refCountByPid;

		pos = window->DC.CursorPos;

		const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
		ImGui::ItemSize(bb, style.FramePadding.y);

		centre = bb.GetCenter();
		if (!ImGui::ItemAdd(bb, id))
			return;

		assert(detail::g_lottieRenderer);
		if (detail::g_lottieRenderer) {
			ImGuiID rid	   = detail::g_lottieRenderer->match(path, size.x, size.y, loop, rate, speed);
			auto lastPidIt = lastPidByWidget.find(id);
			if (lastPidIt == lastPidByWidget.end() || lastPidIt->second != rid) {
				if (lastPidIt != lastPidByWidget.end()) {
					const ImGuiID oldPid = lastPidIt->second;
					auto refIt			 = refCountByPid.find(oldPid);
					if (refIt != refCountByPid.end()) {
						refIt->second -= 1;
						if (refIt->second <= 0) {
							refCountByPid.erase(refIt);
							detail::g_lottieRenderer->discard(oldPid);
						}
					}
				}
				lastPidByWidget[id] = rid;
				refCountByPid[rid] += 1;
				lastTextureByWidget.erase(id);
			}
			void *texture = detail::g_lottieRenderer->image(rid); // get texture from renderer or null if not present
			if (!texture) {
				auto it = lastTextureByWidget.find(id);
				if (it != lastTextureByWidget.end()) {
					texture = it->second;
				}
			}
			if (texture) {
				lastTextureByWidget[id] = texture;
				window->DrawList->AddImage((void *)texture, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
			} else {
				window->DrawList->AddRectFilled(bb.Min, bb.Max, 0xffffffff);
			}
		} else {
			window->DrawList->AddRectFilled(bb.Min, bb.Max, 0xffffffff);
		}
	}



	inline void init() {
		if (!detail::g_lottieRenderer)
			detail::g_lottieRenderer = std::make_unique<LottieAnimationRenderer>();
	}

	inline void destroy() {
		detail::g_lottieRenderer.reset();
	}

	template <typename... Args>
	inline void sync(Args... args) {
		if (detail::g_lottieRenderer) {
			detail::g_lottieRenderer->uploadReadyFramesToSysTex(args...);
		}
	}

#ifdef IMLOTTIE_DEMO
	void demoAnimations(const std::string& demo_folder) {
		ImGui::Begin("Hello, Lottie!"); // Create a window called "Hello, Lottie!" and append into it.
		ImGui::Text("This is some useful animations.");

		auto _ = [&demo_folder](const char *anim) {
			return demo_folder + anim;
		};

		ImLottie::LottieAnimation(_("speaker.json").c_str(), ImVec2(48, 48), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("cubes.json").c_str(), ImVec2(48, 48), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("emojilove.json").c_str(), ImVec2(48, 48), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("car.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("seeu.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("freeside.json").c_str(), ImVec2(64, 64), true, 0);
		// next line
		ImLottie::LottieAnimation(_("valentine.json").c_str(), ImVec2(128, 128), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("jellyfish.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("updown.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("smarthome.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("typing.json").c_str(), ImVec2(64, 64), true, 0);
		// next line
		ImLottie::LottieAnimation(_("explosion.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("heart.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("angrycloud.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("welcome.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("2023.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("fly1.json").c_str(), ImVec2(64, 64), true, 0);
		// next line
		ImLottie::LottieAnimation(_("runcycle.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("email.json").c_str(), ImVec2(64, 64), true, 0);
		ImGui::SameLine();
		ImLottie::LottieAnimation(_("conused.json").c_str(), ImVec2(64, 64), true, 0);

		ImGui::End();
	}
#endif // IMLOTTIE_DEMO

} // end namespace ImLottie
