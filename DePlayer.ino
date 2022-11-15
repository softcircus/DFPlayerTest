#include "Arduino.h"
#include <FS.h>
#include <SPIFFS.h>
#include "DFRobotDFPlayerMini.h"

DFRobotDFPlayerMini myDFPlayer;

// GPIO配置
#define DFPLAYER_BUSY_PIN 34  // BUSY=LOW, Idle=HIGH

// Switchピン番号　On=LOW, Off=HIGH
#define NEXT_SONG_PIN       12
#define PREVIOUS_SONG_PIN   14
#define NEXT_FOLDER_PIN     27
#define PREVIOUS_FOLDER_PIN  26

// SW処理関数
void playNextSong();
void playPreviousSong();
void playNextFolder();
void playPreviousFolder();

struct SwInfo {
  uint8_t pinNo;        // GPIO No
  bool state;           // 状態 true=off, false=on
  void (*func)();       // 処理関数
} swInfo[] = {
  { NEXT_SONG_PIN, true, playNextSong },
  { PREVIOUS_SONG_PIN, true, playPreviousSong },
  { NEXT_FOLDER_PIN, true, playNextFolder },
  { PREVIOUS_FOLDER_PIN, true,  playPreviousFolder  },
  { 0, false, NULL }
};

#define DF_WAIT 400 // DFPlayerにコマンド発行後の待ち時間

uint8_t folderNo;   // フォルダー番号
uint8_t songNo;     // 曲番号
bool repeat;        // リピート処理

uint8_t wrFolderNo;  // 書き込み済みフォルダー番号
uint8_t wrSongNo;    // 書き込み済み曲番号


unsigned long playStartMillis;    // 再生開始時のmillis

void setup()
{
  Serial.begin(9600);  // debugメッセージ出力用

  pinMode( DFPLAYER_BUSY_PIN, INPUT );

  pinMode( NEXT_SONG_PIN, INPUT_PULLUP );
  pinMode( PREVIOUS_SONG_PIN, INPUT_PULLUP );
  pinMode( NEXT_FOLDER_PIN, INPUT_PULLUP );
  pinMode( PREVIOUS_FOLDER_PIN, INPUT_PULLUP );

  SPIFFS.begin();   // ③SPIFFS開始

  // DFPlayer 初期化 
  delay( 1000 );
  Serial2.begin(9600);  // DFPlayerMini接続用(SIO2)
  myDFPlayer.begin(Serial2);
  delay( 2000 );

  myDFPlayer.volume(30);  //DAC出力を使用するので最大値(0 to 30)
  delay( DF_WAIT );

  // 再生制御編集初期化
  if ( !readSetting()){    // 再生情報の読み込み
    // 再生情報なし
    folderNo = 1;
    songNo = 1;
  }
  repeat = false;   // 繰り返しOFF

  // 再生開始
  if(!playSong( folderNo, songNo )){
    // 再生できなかった、先頭に戻して再生
    folderNo = 1;
    songNo = 1;
    playSong( folderNo, songNo );
  }
}

void loop()
{
  // SW状態チェック
  for ( int i = 0; swInfo[ i ].pinNo > 0; i ++ ){
    SwInfo *p = &swInfo[ i ];
    if ( digitalRead( p->pinNo ) != p->state ){
      // 状態変化した
      p->state = !p->state;   // 状態を反転
      if ( p->state == false ){
        // Low でOn状態
        (p->func)();   // 処理関数呼び出し
        break;
      }
    }
  }

  if ( !isBusy() ){
    // 再生していない
    if ( repeat ){
      // 繰り返し中、今の曲を再生
      playSong( folderNo, songNo );
    } else {
      // 次の曲を再生する
      playNextSong();
    }
  }
}

bool isBusy()
{
  return (digitalRead( DFPLAYER_BUSY_PIN ) == LOW ? true : false );
}

void playNextSong()
{
  // repeatをoffにする
  repeat = false;

  songNo ++;
  if ( !playSong( folderNo, songNo )){
    // 次の曲が再生できないので先頭の曲に戻す
    songNo = 1;
    playSong( folderNo, songNo );
  }
}

void playPreviousSong()
{
  // repeatをonにする
  repeat = true;

  if ( songNo > 1 ){  // フォルダーの先頭であれば無条件に今の曲を再生し直し
    if (  millis() - playStartMillis < 3000 ){
      // 再生後3秒以内なら前の曲に戻す、それ以上だったら今の曲を再生し直し
      songNo --;
    }
  }  
  playSong( folderNo, songNo );
}

void playNextFolder()
{
  // repeatをoffにする
  repeat = false;

  // 次のフォルダーの先頭の曲を再生
  folderNo ++;
  songNo = 1;
  if ( !playSong( folderNo, songNo )){
    // 再生できないので先頭のフォルダーを再生
    folderNo = 1;  
    playSong( folderNo, songNo );
  }
}

void playPreviousFolder()
{
  // repeatをoffにする
  repeat = false;

  if ( folderNo > 1 ){
    // 前のフォルダーの先頭の曲を再生
    folderNo --;
    songNo = 1;
    playSong( folderNo, songNo );
  }
}

bool playSong( uint8_t fNo, uint8_t sNo )
{
  Serial.print( "playSong( " );
  Serial.print( fNo );
  Serial.print( ", " );
  Serial.print( sNo );

  myDFPlayer.playFolder( fNo, sNo );  // 指定の曲を再生
  delay( DF_WAIT );
  if ( isBusy()){
    // 再生開始時間セット
    playStartMillis = millis();
    // 再生曲情報をフラッシュに書き込み
    writeSetting();

    Serial.println( " ) = true" );
    return ( true );
  } else {
    Serial.println( " ) = false" );
    return ( false );
  }
}

#define settingFileName "/setting.dat"  // 曲情報ファイル

bool readSetting()
{
  bool rc = false;

  File fp = SPIFFS.open(settingFileName,"r");
  if ( fp.available() >= (sizeof folderNo + sizeof songNo)){
    fp.read(&folderNo, sizeof folderNo);
    fp.read(&songNo, sizeof songNo);

    wrFolderNo = folderNo;
    wrSongNo = songNo;

    rc = true;
  }
  fp.close();
  return ( rc );
}

bool writeSetting()
{
  if ((wrFolderNo == folderNo) && (wrSongNo == songNo)){
    // 書き込む必要なし
    return ( true );
  }

  File fp = SPIFFS.open(settingFileName,"w");
  fp.write(&folderNo, sizeof folderNo );
  fp.write(&songNo, sizeof songNo );
  fp.close();

  wrFolderNo = folderNo;
  wrSongNo = songNo;

  return ( true );
}
