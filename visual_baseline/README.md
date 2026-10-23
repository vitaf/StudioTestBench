# Visual Regression Baseline

Файлы создаются автоматически при первом запуске `run_visual_regression.sh`.

Для принудительного пересоздания:
```bash
./run_visual_regression.sh --baseline
```

Пороги прохождения:
- Diff < 1% пикселей (изменение >30 в любом канале)
- PSNR > 30dB
