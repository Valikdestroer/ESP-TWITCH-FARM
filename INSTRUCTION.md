# 📖 Інструкція користувача: ESP32-S3 Twitch Drops Farmer

Детальна інструкція з налаштування та використання пристрою знаходиться у файлі [README.md](file:///c:/Users/valik/Desktop/progexp/README.md).

---

## 🚀 Короткий чек-лист запуску:

1. **Отримати OAuth Token (`auth-token`)**:
   - В браузері увійдіть на [Twitch.tv](https://www.twitch.tv).
   - Натисніть **F12** -> **Application** -> **Cookies** -> `https://www.twitch.tv`.
   - Скопіюйте значення поля `auth-token`.

2. **Перше підключення**:
   - Підключіть ESP32-S3 до USB.
   - Підключіться до Wi-Fi точки `ESP32-Twitch-Farmer` (пароль `12345678`).
   - Перейдіть на `http://192.168.4.1` та збережіть свій Wi-Fi, OAuth токен і назву гри.

3. **Або налаштування через Serial (USB)**:
   - В моніторі порту (115200 baud) використайте команди:
     ```text
     wifi NazvaMerezhi ParolMerezhi
     token vash_auth_token_tut
     game Rust
     start
     ```
   - Перевірте стан командою `status`.
