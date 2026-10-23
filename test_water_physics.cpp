// Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
// E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.
// Краснодар, Россия / Krasnodar, Russia. Все права защищены. All rights reserved.

#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include "../BioNeuronStudio/src/test_subjects/tadpole/tadpole_habitat_3d.h"

TEST(WaterPhysics, ViscosityTemperature) {
    TadpoleHabitat3D h;
    h.Init();

    // При 20°C — вязкость = 1.0 (норма)
    h.water_temperature = 20.0f; h.UpdateWaterParams();
    EXPECT_NEAR(h.water_viscosity, 1.0f, 0.01f);

    // При 10°C — вязкость выше (>1.0)
    h.water_temperature = 10.0f; h.UpdateWaterParams();
    EXPECT_GT(h.water_viscosity, 1.0f) << "Холодная вода должна быть более вязкой";

    // При 30°C — вязкость ниже (<1.0)
    h.water_temperature = 30.0f; h.UpdateWaterParams();
    EXPECT_LT(h.water_viscosity, 1.0f) << "Тёплая вода должна быть менее вязкой";

    // Вязкость монотонно падает с T
    h.water_temperature = 10.0f; h.UpdateWaterParams(); float v10 = h.water_viscosity;
    h.water_temperature = 20.0f; h.UpdateWaterParams(); float v20 = h.water_viscosity;
    h.water_temperature = 30.0f; h.UpdateWaterParams(); float v30 = h.water_viscosity;
    EXPECT_GT(v10, v20) << "visc(10°C) > visc(20°C)";
    EXPECT_GT(v20, v30) << "visc(20°C) > visc(30°C)";

    printf("[Water] T=10C visc=%.3f | T=20C visc=%.3f | T=30C visc=%.3f\n", v10, v20, v30);
    h.Shutdown();
}

TEST(WaterPhysics, TemperatureDefaults) {
    TadpoleHabitat3D h;
    h.Init();

    EXPECT_FLOAT_EQ(h.water_temperature, 20.0f);
    h.UpdateWaterParams();
    EXPECT_NEAR(h.water_viscosity, 1.0f, 0.01f);

    printf("[Water] Default T=%.1fC, visc=%.3f\n", h.water_temperature, h.water_viscosity);
    h.Shutdown();
}

TEST(WaterPhysics, ExtremeTemperatures) {
    TadpoleHabitat3D h;
    h.Init();

    // 0°C — очень вязкая
    h.water_temperature = 0.0f; h.UpdateWaterParams();
    EXPECT_GT(h.water_viscosity, 1.5f) << "Ледяная вода: сильное сопротивление";

    // 40°C — очень жидкая
    h.water_temperature = 40.0f; h.UpdateWaterParams();
    EXPECT_LT(h.water_viscosity, 0.8f) << "Горячая вода: низкое сопротивление";

    printf("[Water] T=0C visc=%.3f | T=40C visc=%.3f\n", h.water_viscosity,
           (h.water_temperature=0.0f, h.UpdateWaterParams(), h.water_viscosity));
    h.Shutdown();
}
