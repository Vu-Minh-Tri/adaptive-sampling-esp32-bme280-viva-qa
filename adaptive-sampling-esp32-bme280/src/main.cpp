#include <Arduino.h>
#include <esp_sleep.h>
#include <math.h>

// CẤU HÌNH THUẬT TOÁN LẤY MẪU THÍCH NGHI (adaptive sampling).
//
// Ý tưởng: mỗi lần đọc cảm biến xong sẽ so sánh với lần đọc ngay trước đó.
// Mỗi đại lượng có một ngưỡng "được coi là đổi tròn 1 đơn vị" (3 hằng số
// TEMP/HUM/PRESS_THRESHOLD bên dưới). Điểm thay đổi (score) của một lần
// đọc = mức thay đổi LỚN NHẤT trong 3 đại lượng, quy về đơn vị ngưỡng.
// Ví dụ: nhiệt độ đổi 0.25 °C (scoreT = 0.5), độ ẩm đổi 3.0 %RH
// (scoreH = 1.0) thì score của lần đọc đó = 1.0, quyết định theo độ ẩm.
//
// Quy tắc quyết định dựa trên score:
//   score >= 1.0  : môi trường đang đổi nhanh  -> giảm chu kỳ (chia đôi).
//   score <= 0.25 : môi trường đang ổn định    -> đếm lần ổn định,
//                                                    đủ 3 lần thì tăng chu kỳ.
//   còn lại       : môi trường đổi vừa phải    -> giữ nguyên chu kỳ.
#define INITIAL_INTERVAL_SEC 10   // chu kỳ lấy mẫu lúc mới khởi động (giây)
#define MIN_INTERVAL_SEC      2   // chu kỳ ngắn nhất cho phép (giây)
#define MAX_INTERVAL_SEC     60   // chu kỳ dài nhất cho phép (giây)

// Ngưỡng để mỗi đại lượng "tròn 1 điểm thay đổi":
#define TEMP_THRESHOLD       0.5   // nhiệt độ đổi 0.5 °C  -> điểm của T = 1.0
#define HUM_THRESHOLD        3.0   // độ ẩm đổi 3.0 %RH   -> điểm của H = 1.0
#define PRESS_THRESHOLD      1.0   // áp suất đổi 1.0 hPa -> điểm của P = 1.0

// Số lần đọc ổn định liên tiếp (score <= 0.25) cần có
// trước khi hệ thống cho phép giãn chu kỳ lấy mẫu.
#define STABLE_REQUIRED      3

// Cấu trúc lưu kết quả một lần đọc cảm biến: nhiệt độ (°C),
// độ ẩm (%RH) và áp suất (hPa).
struct SensorData {
  float temperature;
  float humidity;
  float pressure;
};

// CÁC BIẾN RTC_DATA_ATTR: GIỮ GIÁ TRỊ SONG SONG VỚI DEEP SLEEP.
//
// Khi ESP32 ngủ sâu, RAM thông thường bị mất điện, nhưng vùng RAM của
// khối RTC (8 KB) vẫn được cấp điện nhẹ. Vì vậy mọi biến trạng thái cần
// "sống sót" qua mỗi lần dậy phải khai báo RTC_DATA_ATTR. Nhờ đó thuật
// toán hoạt động liên tục giữa các chu kỳ: dậy -> so sánh với lần trước
// -> quyết định chu kỳ mới -> ngủ, mà không cần thẻ nhớ hay flash.
RTC_DATA_ATTR uint32_t wakeCount = 0;      // đã dậy bao nhiêu lần từ khi nạp code
RTC_DATA_ATTR uint32_t sampleIndex = 0;    // đã lấy tới mẫu thứ mấy trong testData
RTC_DATA_ATTR uint32_t currentInterval = INITIAL_INTERVAL_SEC;  // chu kỳ hiện tại (giây)
RTC_DATA_ATTR uint32_t stableCount = 0;    // đếm số lần đọc ổn định liên tiếp
RTC_DATA_ATTR bool hasPreviousReading = false;  // đã có lần đọc trước để so sánh chưa
RTC_DATA_ATTR SensorData previousReading;  // giá trị lần đọc trước (mốc so sánh)

// DỮ LIỆU GIẢ LẬP (DÙNG TẠM KHI CHƯA CÓ BME280 THẬT).
//
// Board thật chưa có cảm biến nên đầu vào được giả lập bằng bảng 11 mẫu
// dưới đây, đúng theo kịch bản thử nghiệm của milestone 5:
//   mẫu 0 -> 6  : môi trường ổn định (các giá trị đổi rất nhỏ)
//   mẫu 7       : đổi đột ngột (+1.4 °C, +4.3 %RH, +1.7 hPa) -> score 1.7
//   mẫu 8 -> 10 : lại ổn định sau cú đổi lớn
// Nhờ bảng này có thể đối chiếu trực tiếp log chạy thật trên ESP32 với
// dự đoán lý thuyết của thuật toán. Khi BME280 đến hàng, chỉ cần thay
// chỗ gán "current" trong setup() bằng lệnh đọc cảm biến thật
// (bme.readTemperature() / readHumidity() / readPressure()).
const SensorData testData[] = {
  {25.00, 60.0, 1000.0},
  {25.10, 60.2, 1000.2},
  {25.15, 60.1, 1000.3},
  {25.20, 60.2, 1000.4},
  {25.30, 60.1, 1000.3},
  {25.30, 60.2, 1000.2},
  {25.40, 60.2, 1000.3},
  {26.00, 64.5, 1002.0},
  {26.10, 64.6, 1002.1},
  {26.15, 64.6, 1002.2},
  {26.20, 64.7, 1002.1}
};

const uint32_t DATA_COUNT =
    sizeof(testData) / sizeof(testData[0]);

// CẬP NHẬT CHU KỲ LẤY MẪU THEO MỨC THAY ĐỔI CỦA MÔI TRƯỜNG.
//
// Đầu vào: lần đọc hiện tại. Hàm so sánh với previousReading (lần đọc
// trước, nằm trong RTC RAM) rồi điều chỉnh currentInterval cho phù hợp.
void updateSamplingInterval(const SensorData& current) {

  // Lần đọc đầu tiên chưa có mốc nào để so sánh, nên chỉ lưu lại làm mốc
  // cho các lần sau và giữ nguyên chu kỳ ban đầu.
  if (!hasPreviousReading) {
    previousReading = current;
    hasPreviousReading = true;

    Serial.println("First reading: no comparison yet.");
    return;
  }

  // Điểm thay đổi của từng đại lượng = |chênh lệch| / ngưỡng của nó.
  // LƯU Ý: phải dùng fabsf/fmaxf (phiên bản cho float) thay vì abs/max
  // của Arduino. abs()/max() là macro dành cho số nguyên; gọi nhầm với
  // float sẽ cắt phần lẻ hoặc so sánh sai -> score bị tính sai âm thầm.
  // Đây là một trong các lỗi đã được sửa trong bản code này.
  float scoreT = fabsf(current.temperature - previousReading.temperature)
                 / TEMP_THRESHOLD;
  float scoreH = fabsf(current.humidity - previousReading.humidity)
                 / HUM_THRESHOLD;
  float scoreP = fabsf(current.pressure - previousReading.pressure)
                 / PRESS_THRESHOLD;

  // Đại lượng nào đổi mạnh nhất "theo thang đo của nó" sẽ ra quyết định,
  // vì chỉ cần một đại lượng biến động rõ là đã đáng phải lấy mẫu dày hơn.
  float score = fmaxf(scoreT, fmaxf(scoreH, scoreP));

  Serial.print("Change score: ");
  Serial.println(score, 3);

  if (score >= 1.0) {

    // ĐỔI NHANH: chia đôi chu kỳ để bắt kịp biến động.
    // (currentInterval + 1) / 2 là phép chia đôi LÀM TRÒN LÊN đối với số
    // nguyên, ví dụ 23 -> 12 (thay vì 11), đảm bảo không bỏ sót biến động.
    // max(...) chặn dưới: chu kỳ không bao giờ nhỏ hơn MIN_INTERVAL_SEC.
    currentInterval =
        max((uint32_t)MIN_INTERVAL_SEC,
            (uint32_t)((currentInterval + 1U) / 2U));

    stableCount = 0;   // môi trường đang động -> bộ đếm ổn định bị xóa

    Serial.println("Rapid change detected.");

  } else if (score <= 0.25) {

    // ỔN ĐỊNH: tăng bộ đếm, và phải đạt đủ STABLE_REQUIRED lần LIÊN TIẾP
    // thì mới được giãn chu kỳ. Điều kiện "liên tiếp" giúp tránh tăng chu kỳ
    // sớm khi môi trường chỉ yên tĩnh thoáng qua rồi lại biến động.
    stableCount++;

    Serial.print("Stable count: ");
    Serial.println(stableCount);

    if (stableCount >= STABLE_REQUIRED) {

      // Giãn chu kỳ khoảng 1.5 lần: công thức * 1.5 + 0.5 rồi ép về số
      // nguyên chính là LÀM TRÒN TỚI SỐ GẦN NHẤT, ví dụ 15 * 1.5 = 22.5
      // -> 23 giây. min(...) chặn trên: không vượt quá MAX_INTERVAL_SEC.
      currentInterval =
          min((uint32_t)MAX_INTERVAL_SEC,
              (uint32_t)(currentInterval * 1.5 + 0.5));

      stableCount = 0;   // reset để chờ đủ 3 lần ổn định tiếp theo mới tăng nữa

      Serial.println("Stable environment: increasing interval.");
    }

  } else {

    // ĐỔI VỪA PHẢI (0.25 < score < 1.0): giữ nguyên chu kỳ và xóa bộ đếm
    // ổn định, vì môi trường chưa đủ yên tĩnh để tính là "ổn định".
    stableCount = 0;
    Serial.println("Moderate change: keeping interval.");
  }

  // Lưu lần đọc hiện tại làm mốc so sánh cho lần dậy kế tiếp
  // (ghi đè vào biến RTC nên giá trị không mất khi ngủ sâu).
  previousReading = current;
}

// SETUP: CHẠY ĐẦU TIÊN SAU MỖI LẦN ESP32 DẬY (kể cả dậy từ deep sleep).
//
// Luồng một chu kỳ lấy mẫu: in trạng thái -> đọc dữ liệu (giả lập) ->
// cập nhật chu kỳ theo mức thay đổi -> hẹn giờ dậy -> ngủ sâu.
// loop() không bao giờ được chạy, vì setup() luôn kết thúc bằng
// esp_deep_sleep_start() ở cả 2 nhánh (hết dữ liệu hoặc chưa hết).
void setup() {

  Serial.begin(115200);
  delay(1000);   // chờ UART ổn định và serial monitor kịp kết nối

  wakeCount++;   // tăng bộ đếm lần dậy (giá trị nằm trong RTC RAM nên tăng dần qua các lần ngủ)

  Serial.println();
  Serial.println("================================");
  Serial.println("MILESTONE 5: ADAPTIVE + DEEP SLEEP");
  Serial.println("================================");

  // In trạng thái phục vụ kiểm chứng trên serial: đây chính là các dòng
  // dùng để đối chiếu log chạy thật với dự đoán thuật toán trong báo cáo.
  Serial.print("Wake count: ");
  Serial.println(wakeCount);

  Serial.print("Sample index: ");
  Serial.println(sampleIndex);

  Serial.print("Previous interval: ");
  Serial.print(currentInterval);
  Serial.println(" seconds");

  // Phân biệt lần dậy này do hẹn giờ (timer) hay do cắm điện / nhấn RESET.
  // Nếu log hiện "TIMER" liên tiếp nghĩa là deep sleep + timer wakeup
  // hoạt động đúng; lần đầu cắm điện sẽ hiện "POWER-ON / OTHER".
  esp_sleep_wakeup_cause_t cause =
      esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wake-up cause: TIMER");
  } else {
    Serial.println("Wake-up cause: POWER-ON / OTHER");
  }

  // Đã lấy hết bảng dữ liệu giả lập: kết thúc kịch bản thử nghiệm.
  // Không hẹn giờ dậy nữa (tắt mọi wakeup source) rồi vẫn cho ngủ sâu:
  // chip nằm yên ở mức tiêu thụ rất thấp, chỉ dậy khi người dùng nhấn
  // RESET. (Bản cũ dùng while(true) delay(1000) ở đây, chip phải chạy
  // vòng lặp ~40 mA suốt đêm — đã sửa thành ngủ sâu như dưới đây.)
  if (sampleIndex >= DATA_COUNT) {
    Serial.println("Simulation completed.");
    Serial.println("Press RESET (EN) to run again.");

    Serial.flush();   // đợi phần chữ còn lại trong buffer UART gửi xong rồi mới ngủ

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
  }

  // Lấy mẫu giả lập tiếp theo trong bảng testData.
  // Khi có BME280 thật: thay dòng này bằng lệnh đọc cảm biến thật.
  SensorData current = testData[sampleIndex];

  Serial.println("--------------------------------");

  Serial.print("Temperature: ");
  Serial.println(current.temperature, 2);

  Serial.print("Humidity: ");
  Serial.println(current.humidity, 2);

  Serial.print("Pressure: ");
  Serial.println(current.pressure, 2);

  // So sánh với lần đọc trước và điều chỉnh chu kỳ lấy mẫu cho phù hợp.
  updateSamplingInterval(current);

  sampleIndex++;   // lần dậy sau sẽ lấy mẫu kế tiếp trong bảng

  Serial.println("--------------------------------");

  // In chu kỳ mới, sau đó hẹn giờ cho ESP32 dậy đúng sau khoảng thời gian đó.
  // LƯU Ý ĐƠN VỊ: esp_sleep_enable_timer_wakeup nhận MICRO GIÂY (us),
  // nên phải nhân số giây với 1.000.000 và dùng uint64_t để không tràn số.
  Serial.print("Next sampling interval: ");
  Serial.print(currentInterval);
  Serial.println(" seconds");

  Serial.println("Entering deep sleep...");

  Serial.flush();   // đẩy hết log ra UART trước khi ngủ, tránh mất dòng cuối

  esp_sleep_enable_timer_wakeup(
      (uint64_t)currentInterval * 1000000ULL
  );

  // Từ điểm này đến khi timer hết giờ, chip gần như ngừng tiêu thụ điện:
  // RAM thường bị mất, chỉ các biến RTC_DATA_ATTR phía trên được giữ lại.
  esp_deep_sleep_start();
}

// Không dùng: ESP32 đã ngủ sâu ngay trong setup() nên loop() không bao giờ
// chạy. Arduino framework vẫn yêu cầu hàm này tồn tại thì mới build được.
void loop() {
  // Trống có chủ đích.
}
