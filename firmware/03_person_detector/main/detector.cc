/*
 * TensorFlow Lite Micro person detector.
 * Model: 96x96 grayscale int8 MobileNet (~300 KB), from the esp-tflite-micro example.
 *   weights      -> stay in FLASH (read through the cache, never copied)
 *   tensor arena -> RAM: inputs, outputs and intermediate activations
 */
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "app.h"

static const char *TAG = "detector";

namespace {
#if CONFIG_NN_OPTIMIZED
constexpr int kScratchSize = 60 * 1024;   // extra buffers used by the ESP-NN accelerated kernels
#else
constexpr int kScratchSize = 0;
#endif
constexpr int kTensorArenaSize = 100 * 1024 + kScratchSize;

tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
}  // namespace

esp_err_t detector_init(void)
{
    const tflite::Model *model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %lu != supported %d", (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_FAIL;
    }

    // Internal RAM is faster; fall back to PSRAM if Wi-Fi + camera used it up
    uint8_t *arena = (uint8_t *)heap_caps_malloc_prefer(kTensorArenaSize, 2,
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!arena) {
        ESP_LOGE(TAG, "Could not allocate %d byte tensor arena", kTensorArenaSize);
        return ESP_ERR_NO_MEM;
    }

    // Only the 5 operations this network uses get linked in
    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddAveragePool2D();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddReshape();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(model, resolver, arena, kTensorArenaSize);
    interpreter = &static_interpreter;
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        return ESP_FAIL;
    }
    input = interpreter->input(0);
    ESP_LOGI(TAG, "Model ready: %u KB in flash, arena uses %u KB (%s)",
             (unsigned)(g_person_detect_model_data_len / 1024),
             (unsigned)(interpreter->arena_used_bytes() / 1024),
             esp_ptr_external_ram(arena) ? "PSRAM" : "internal RAM");
    return ESP_OK;
}

int8_t *detector_input(void)
{
    return input ? input->data.int8 : nullptr;
}

esp_err_t detector_run(int *person_score)
{
    if (interpreter->Invoke() != kTfLiteOk) {
        return ESP_FAIL;
    }
    TfLiteTensor *out = interpreter->output(0);
    float p = (out->data.int8[kPersonIndex] - out->params.zero_point) * out->params.scale;
    int s = (int)(p * 100 + 0.5f);
    *person_score = s < 0 ? 0 : (s > 100 ? 100 : s);
    return ESP_OK;
}
