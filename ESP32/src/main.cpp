#include "main.h"

/*
  Program interupsi pin encoder
  kanal A motor 1
*/
void ICACHE_RAM_ATTR EN1_ISR()
{
  portENTER_CRITICAL(&mux);
  m1.isrHandler();
  portEXIT_CRITICAL(&mux);
}

/*
  Program interupsi pin encoder
  kanal A motor 1
*/
void ICACHE_RAM_ATTR EN2_ISR()
{
  portENTER_CRITICAL(&mux);
  m2.isrHandler();
  portEXIT_CRITICAL(&mux);
}

/*
  Program interupsi pin encoder
  kanal A motor 1
*/
void ICACHE_RAM_ATTR EN3_ISR()
{
  portENTER_CRITICAL(&mux);
  m3.isrHandler();
  portEXIT_CRITICAL(&mux);
}

/*
  Mengatur kecepatan gerak robot melalui terminal teleop
  Digunakan untuk pengujian gerak robot
*/
void onCmdVel(const geometry_msgs::Twist &msg_data)
{
  vel_data = msg_data;
  last_command_time = millis();
}

/*
  Mengatur ulang semua kendali dan posisi robot
  Akan dijalankan ketika mendapat perintah dari topik
  /reset dengan menekan tombol '3' di terminal Teleop
*/
void onResetPose(const std_msgs::Empty &msg_data)
{
  m1.encoder_tick_acc = 0;
  m2.encoder_tick_acc = 0;
  m3.encoder_tick_acc = 0;
  heading_deg = 0;
  base.pos_x = 0;
  base.pos_y = 0;
  goal_x.setpoint = 0;
  goal_y.setpoint = 0;
  goal_w.setpoint = 0;
  goal_x.reset();
  goal_y.reset();
  goal_w.reset();
}

/*
  Menerima data marker setiap ada penandaan posisi
  baru di RViz dan menyimpannya di variabel marker_data
  untuk digunakan sebagai kendali posisi multipoint
*/
void onMarkerSet(const visualization_msgs::Marker &msg_data)
{
  marker_data = msg_data;
}

/*
  Untuk memulai menjalankan kendali posisi
  ketika menerima perintah dari topik
  /marker_follower
  Untuk mengirimkan perintah, tekan tombol 2 pada
  terminal Teleop
*/
void onMarkerFollower(const std_msgs::Empty &msg_data)
{
  DEBUG.println("MARKER FOLLOWER STARTED");
  finish = false;
  for (int i = 0; i < marker_data.points_length; i++)
  {
    DEBUG.printf("X : %.f Y : %.f Z : %.f\n", marker_data.points[i].x, marker_data.points[i].y, marker_data.points[i].z);
  }
}
/*
  LED akan berkedip setiap 300ms saat robot
  belum tersambung dan berkedip setiap 2s
  ketika robot tersambung ke ROS
*/
void blink(void *parameters)
{
  pinMode(LED_BUILTIN, OUTPUT);
  for (;;)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    vTaskDelay(is_ros_ready ? 2000 / portTICK_PERIOD_MS : 300 / portTICK_PERIOD_MS);
  }
}

/*
  Inisialisasi ros node
  Mendaftarkan publisher dan subscriber
  - vel_sub adalah topik kendali manual robot menggunakan teleop
  - rst_pos_sub adalah topik untuk mereset sistem kendali dan posisi robot
  - marker_sub adalah topik untuk menerima data setpoint multipoint dari
    perintah masukan berupa data marker di RViz
  - marker_follower_sub adalah topik untuk memulai kendali posisi multipoint
  - pose_pub adalah topik untuk mengirimkan data posisi robot sekarang
*/

void setupSubscriberAndPublisher()
{
  nh.initNode();
  nh.subscribe(vel_sub);
  nh.subscribe(rst_pos_sub);
  nh.subscribe(marker_sub);
  nh.subscribe(marker_follower_sub);
  nh.advertise(pose_pub);
  heading_deg = 0;
  is_ros_ready = true;
}

void initNode(void *parameters)
{
#if defined(ROS_USE_SERIAL)
  setupSubscriberAndPublisher();
#endif
  for (;;)
  {
#if !defined(ROS_USE_SERIAL)
    while (true)
    {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      if (WiFi.softAPgetStationNum() > 0)
        break;
      else
        is_ros_ready = false;
    }
    if (client.connected() != 1)
    {
      setupSubscriberAndPublisher();
    }
    if (client.connected() == 1)
    {
      nh.spinOnce();
    }
#else
    nh.spinOnce();
#endif
    vTaskDelay(PUBLISH_DELAY_MS / portTICK_PERIOD_MS);
  }
}

/*
  Mempublish message pada topik ROS
  pose_pub digunakan untuk mengirim data
  posisi robot saat ini dalam x (meter), y(meter),
  theta(radian)
*/
void publishMessage(void *parameter)
{
  for (;;)
  {
    if (is_ros_ready)
    {
      pose_pub.publish(&pose_data);
    }
    vTaskDelay(PUBLISH_DELAY_MS / portTICK_PERIOD_MS);
  }
}

/*
  Data kompas akan terus increment/decrement ketika robot terus
  menerus berputar. Kompas perlu dikalibrasi terlebih dahulu
  menggunakan contoh program pada library. Data yang dihasilkan
  masih dalam format degree, bukan radian
*/
void readCompass(void *parameters)
{
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  QMC5883LCompass compass;
  compass.init();
  compass.setCalibration(-622, 682, -1491, 341, 0, 612);
  compass.read();
  last_compass_reading = compass.getAzimuth();
  heading_deg = last_compass_reading;
  for (;;)
  {
    compass.read();
    int now = map(compass.getAzimuth(), 0, 359, 359, 0);
    if (abs(now - last_compass_reading) > 300)
    {
      int offset;
      if (now - last_compass_reading < 0)
      {
        offset = (360 - last_compass_reading) + now;
        heading_deg = heading_deg + offset;
      }
      else
      {
        offset = (360 - now) + last_compass_reading;
        heading_deg = heading_deg - (last_compass_reading + offset);
      }
    }
    else
      heading_deg = heading_deg + (now - last_compass_reading);

    last_compass_reading = now;
    vTaskDelay(PUBLISH_DELAY_MS / portTICK_PERIOD_MS);
  }
}

/*
  Robot menerima perintah dari rostopic cmd_vel
  Robot dapat bergerak ketika PC terkoneksi ke AP robot dan
  perintah terakhir kurang dari 0.5 detik yang lalu
  untuk menghindari robot bergerak terus menerus ketika
  terputus dari PC
*/
void moveBase(void *parameters)
{
  for (;;)
  {
    if (millis() - last_command_time < 500)
    {
      float lin_x = -vel_data.linear.y;
      float lin_y = vel_data.linear.x;
      float ang_z = vel_data.angular.z;
      base.setSpeed(lin_x, lin_y, ang_z);
    }
    else
      base.setSpeed(0, 0, 0);
    vTaskDelay(10);
  }
}

/*
  Menghitung RPM motor lalu mengkonversinya
  ke satuan meter/detik, kalkulasi diatur setiap
  5 ms. Fungsi Kinematic::calculatePosition
  berfungsi untuk menghitung posisi robot sekarang
  dengan rumus odometri. Penjelasan rumus ada didalam
  pustaka kinematic.cpp
*/
void odometry(void *parameters)
{
  const int sampling_time_ms = 5;
  for (;;)
  {
    m1.calculateRpm(sampling_time_ms);
    m2.calculateRpm(sampling_time_ms);
    m3.calculateRpm(sampling_time_ms);
    base.calculatePosition(heading_deg);
    pose_data.x = base.pos_x;
    pose_data.y = base.pos_y;
    pose_data.theta = base.pos_th;
    vTaskDelay(sampling_time_ms / portTICK_PERIOD_MS);
  }
}

/*
  Kendali PID posisi robot dengan umpan balik
  posisi robot saat ini. Program akan membaca
  array yang didapat dari topik ROS markers
  yang disimpan di variabel marker_data
  sampai array terakhir
*/
void poseControl(void *parameters)
{
  for (;;)
  {
    if (!finish)
    {
      float lin_x = goal_x.compute(base.pos_x);
      float lin_y = goal_y.compute(base.pos_y);
      float ang_z = goal_w.compute(base.pos_th);
      base.setSpeed(lin_x, lin_y, ang_z);
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (abs(goal_x.setpoint - base.pos_x) < 0.01 and abs(goal_y.setpoint - base.pos_y) < 0.01 and abs(goal_w.setpoint - base.pos_th) < 0.01)
    {
      if (marker_array_position < marker_data.points_length)
      {
        goal_x.reset();
        goal_y.reset();
        goal_w.reset();
        goal_x.setpoint = marker_data.points[marker_array_position].x;
        goal_y.setpoint = marker_data.points[marker_array_position].y;
        goal_w.setpoint = 0;
        marker_array_position++;
      }
      else
      {
        finish = true;
        marker_array_position = 0;
      }
    }
  }
}

void setup()
{
#if !defined(ROS_USE_SERIAL)
  DEBUG.begin(115200); // Memulai koneksi serial
#endif
  analogWriteFrequency(10000); // Atur frekuensi PWM

  /*
    Mengatur program yang akan digunakan sebagai
    layanan interupsi eksternal
  */
  attachInterrupt(digitalPinToInterrupt(m1.en_a), EN1_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(m2.en_a), EN2_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(m3.en_a), EN3_ISR, FALLING);

  /*
    Pengaturan nilai PID untuk masing-masing motor
  */
  m1.pid(2, 0.05, 1, 255);
  m2.pid(0.5, 0.05, 1, 255);
  m3.pid(5, 0.05, 1, 255);

  /*
    Fungsi Kinematic::setMotor akan otomatis
    menyesuaikan kinematika dengan jumlah objek
    motor yang dimasukkan
  */
  base.setMotor(m1, m2, m3);

  /*
    Menjalankan task RTOS
    xTaskCreatePinnedToCore(fungsi, "nama fungsi", alokasi memori, prioritas, task handle, core);
    ESP32 memiliki 3 core, yaitu core 0, core 1, dan ULP
    Sebisa mungkin prioritas task disamakan untuk menghindari crash
    Task yang paling sering dijalankan diberikan prioritas paling tinggi
    sem_i2c = xSemaphoreCreateMutex();
  */
  xTaskCreatePinnedToCore(wifiSetup, "wifi setup", 10000, NULL, 5, &wifi_task, 0);        // Pengaturan akses poin
  xTaskCreatePinnedToCore(blink, "blink", 1000, NULL, 2, &blink_task, 1);                 // Test apakah RTOS dapat berjalan
  xTaskCreatePinnedToCore(initNode, "node", 5000, NULL, 5, &ros_task, 0);                 // Inisialisasi ros node
  xTaskCreatePinnedToCore(publishMessage, "publisher", 10000, NULL, 2, &ros_pub_task, 1); // Task publish ros messsage
  xTaskCreatePinnedToCore(readCompass, "compass", 10000, NULL, 2, &compass_task, 1);      // Membaca sensor kompas
  xTaskCreatePinnedToCore(moveBase, "base", 5000, NULL, 2, &motor_task, 1);               // Menggerakkan base robot melalui perintah cmd_vel
  xTaskCreatePinnedToCore(odometry, "odometry", 5000, NULL, 2, &odometry_task, 1);             // Menghitung posisi robot saat ini
  xTaskCreatePinnedToCore(poseControl, "pose control", 10000, NULL, 2, &pose_control_task, 1); // Mengontrol pergerakan robot dengan kendali PID
}
void loop()
{
  vTaskDelay(1);
}