# 上游来源与第三方权利声明

本项目基于 [Espressif esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) 的本地门铃例程。公开仓库保留该快照中的组件、示例、预编译库、文件头和许可证，不将其全部描述为个人原创。

## 随仓库保留的许可证

- [`components/av_render/LICENSE`](components/av_render/LICENSE)
- [`components/codec_board/LICENSE`](components/codec_board/LICENSE)
- [`components/esp_peer/LICENSE`](components/esp_peer/LICENSE)，包含 Peer 预编译库的使用条件
- [`components/media_lib_utils/LICENSE`](components/media_lib_utils/LICENSE)
- [`components/esp_webrtc/impl/kvs_signaling/sigv4/LICENSE`](components/esp_webrtc/impl/kvs_signaling/sigv4/LICENSE)

其他源文件继续适用各自文件头和依赖组件的许可声明；`managed_components` 首次构建时在线获取，亦须遵守其原许可证。公开归档未新增覆盖所有项目贡献、文档、视频与第三方资料的统一许可证。

## 演示证书与密钥

`solutions/doorbell_local/main/certs` 中的证书及密钥是原例程公开演示材料。本次将私钥 PEM 去除换行形式差异后与上游公开文件比较，内容一致；保留它是为了不破坏样例构建。该密钥不具有私人保密性，不能作为真实部署凭据；实际部署需要独立证书、密钥和身份鉴权设计。

## 硬件、音频与视频

`hardware/reference` 为配套硬件的原厂参考文档，保留各厂商原有权利；屏幕资料的存在不表示该模块已在项目中使用。AAC 为快照自带的预录提示音，不能作为原创语音模型或中文语音识别成果。Release 的两个 MP4 为本项目已有的讲解与演示文件，外部参考视频未再发布。
