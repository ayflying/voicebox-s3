package main

import (
	"bytes"
	"context"
	"crypto/md5"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/gogf/gf/v2/os/gfile"
)

var (
	authToken   string
	adminToken  string
	upstreamASR string
	firmwareDir string
	listenPort  int
)

var versionRe = regexp.MustCompile(`^\d+\.\d+\.\d+$`)

func main() {
	authToken = envDefault("AUTH_TOKEN", "changeme-device-token")
	adminToken = envDefault("ADMIN_TOKEN", "changeme-admin-token")
	upstreamASR = envDefault("UPSTREAM_ASR", "http://asr:9000")
	firmwareDir = envDefault("FIRMWARE_DIR", "/firmware")
	listenPort = atoiDefault(os.Getenv("GATEWAY_PORT"), 18080)

	_ = os.MkdirAll(firmwareDir, 0o755)
	ensureManifest()

	s := g.Server()
	s.SetPort(listenPort)
	s.SetServerRoot("")

	// 设备接口（需 X-Auth-Token）
	s.Group("/", func(group *ghttp.RouterGroup) {
		group.Middleware(authMiddleware)
		group.POST("/transcribe", transcribeHandler)
		group.GET("/api/ota/version", versionHandler)
		group.GET("/api/ota/firmware", firmwareHandler)
	})

	// 管理员接口（需 X-Admin-Token，独立令牌）
	s.Group("/api/admin", func(group *ghttp.RouterGroup) {
		group.Middleware(adminMiddleware)
		group.POST("/ota/push", pushHandler)
	})

	s.BindHandler("/health", func(r *ghttp.Request) {
		r.Response.WriteJson(g.Map{"status": "ok"})
	})

	g.Log().Infof(context.Background(), "gateway listening on :%d", listenPort)
	s.Run()
}

func envDefault(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func atoiDefault(s string, def int) int {
	if s == "" {
		return def
	}
	n, err := strconv.Atoi(s)
	if err != nil {
		return def
	}
	return n
}

// ---------- 中间件 ----------

func authMiddleware(r *ghttp.Request) {
	if r.Header.Get("X-Auth-Token") != authToken {
		r.Response.WriteStatusExit(http.StatusUnauthorized, g.Map{"error": "unauthorized"})
		return
	}
	r.Middleware.Next()
}

func adminMiddleware(r *ghttp.Request) {
	if r.Header.Get("X-Admin-Token") != adminToken {
		r.Response.WriteStatusExit(http.StatusUnauthorized, g.Map{"error": "admin unauthorized"})
		return
	}
	r.Middleware.Next()
}

// ---------- /transcribe：将设备裸 PCM 封装为 WAV，再调用现成 Whisper HTTP API ----------

func transcribeHandler(r *ghttp.Request) {
	pcm := r.GetBody()
	if len(pcm) == 0 {
		r.Response.WriteStatusExit(http.StatusBadRequest, g.Map{"error": "empty body"})
		return
	}

	var payload bytes.Buffer
	writer := multipart.NewWriter(&payload)
	file, err := writer.CreateFormFile("file", "voice.wav")
	if err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": "create audio form failed"})
		return
	}
	if _, err = file.Write(pcmToWav(pcm)); err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": "encode wav failed"})
		return
	}
	_ = writer.WriteField("model", "whisper-1")
	_ = writer.WriteField("language", "zh")
	if err = writer.Close(); err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": "close audio form failed"})
		return
	}

	req, err := http.NewRequestWithContext(r.Context(), http.MethodPost, upstreamASR+"/v1/audio/transcriptions", &payload)
	if err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": "build upstream request failed"})
		return
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		r.Response.WriteStatusExit(http.StatusBadGateway, g.Map{"error": "upstream error: " + err.Error()})
		return
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		r.Response.WriteStatusExit(http.StatusBadGateway, g.Map{"error": "read upstream response failed"})
		return
	}
	if resp.StatusCode != http.StatusOK {
		r.Response.WriteStatusExit(http.StatusBadGateway, g.Map{"error": "upstream status " + strconv.Itoa(resp.StatusCode)})
		return
	}
	r.Response.Header().Set("Content-Type", "application/json")
	r.Response.Write(data)
}

// pcmToWav 将 ESP32 上传的 16 kHz、16 bit、单声道 little-endian PCM 封装为标准 WAV。
func pcmToWav(pcm []byte) []byte {
	const sampleRate uint32 = 16000
	const channels uint16 = 1
	const bitsPerSample uint16 = 16
	byteRate := sampleRate * uint32(channels) * uint32(bitsPerSample) / 8
	blockAlign := channels * bitsPerSample / 8
	out := make([]byte, 44+len(pcm))
	copy(out[0:4], "RIFF")
	binary.LittleEndian.PutUint32(out[4:8], uint32(36+len(pcm)))
	copy(out[8:12], "WAVEfmt ")
	binary.LittleEndian.PutUint32(out[16:20], 16)
	binary.LittleEndian.PutUint16(out[20:22], 1)
	binary.LittleEndian.PutUint16(out[22:24], channels)
	binary.LittleEndian.PutUint32(out[24:28], sampleRate)
	binary.LittleEndian.PutUint32(out[28:32], byteRate)
	binary.LittleEndian.PutUint16(out[32:34], blockAlign)
	binary.LittleEndian.PutUint16(out[34:36], bitsPerSample)
	copy(out[36:40], "data")
	binary.LittleEndian.PutUint32(out[40:44], uint32(len(pcm)))
	copy(out[44:], pcm)
	return out
}

// ---------- OTA：版本查询 ----------

func ensureManifest() {
	p := filepath.Join(firmwareDir, "manifest.json")
	if gfile.Exists(p) {
		return
	}
	defaultManifest := g.Map{
		"version":      "0.0.0",
		"md5":          "",
		"size":         0,
		"description":  "initial placeholder, no firmware pushed yet",
		"published_at": "",
		"url":          "/api/ota/firmware",
	}
	b, _ := json.MarshalIndent(defaultManifest, "", "  ")
	_ = gfile.PutBytes(p, b)
}

func loadManifest() g.Map {
	p := filepath.Join(firmwareDir, "manifest.json")
	b := gfile.GetBytes(p)
	m := g.Map{}
	_ = json.Unmarshal(b, &m)
	if m["version"] == nil {
		m["version"] = "0.0.0"
	}
	return m
}

func versionHandler(r *ghttp.Request) {
	m := loadManifest()
	latest := gconvString(m["version"])
	current := r.GetQuery("current").String()
	if current != "" && !versionLess(current, latest) {
		r.Response.WriteJson(g.Map{"upToDate": true, "current": current})
		return
	}
	r.Response.WriteJson(m)
}

func firmwareHandler(r *ghttp.Request) {
	m := loadManifest()
	p := filepath.Join(firmwareDir, "firmware.bin")
	if !gfile.Exists(p) {
		r.Response.WriteStatusExit(http.StatusNotFound, g.Map{"error": "no firmware available"})
		return
	}
	r.Response.Header().Set("X-MD5", gconvString(m["md5"]))
	r.Response.Header().Set("Content-Type", "application/octet-stream")
	r.Response.ServeFile(p)
}

// ---------- OTA：管理员推送 ----------

func pushHandler(r *ghttp.Request) {
	version := r.GetForm("version").String()
	description := r.GetForm("description").String()
	if !versionRe.MatchString(version) {
		r.Response.WriteStatusExit(http.StatusBadRequest, g.Map{"error": "invalid version, expect X.Y.Z"})
		return
	}
	upfile := r.GetUploadFile("file")
	if upfile == nil {
		r.Response.WriteStatusExit(http.StatusBadRequest, g.Map{"error": "missing file field"})
		return
	}
	saved, err := upfile.Save(os.TempDir(), false)
	if err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": err.Error()})
		return
	}
	data, err := os.ReadFile(saved)
	if err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": err.Error()})
		return
	}
	sum := md5.Sum(data)
	md5hex := hex.EncodeToString(sum[:])

	// 原子写入 firmware.bin
	tmpBin := filepath.Join(firmwareDir, "firmware.bin.tmp")
	if err := os.WriteFile(tmpBin, data, 0o644); err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": err.Error()})
		return
	}
	_ = os.Rename(tmpBin, filepath.Join(firmwareDir, "firmware.bin"))

	// 写 manifest（含 md5）
	manifest := g.Map{
		"version":      version,
		"md5":          md5hex,
		"size":         len(data),
		"description":  description,
		"published_at": time.Now().UTC().Format(time.RFC3339),
		"url":          "/api/ota/firmware",
	}
	b, _ := json.MarshalIndent(manifest, "", "  ")
	tmpMf := filepath.Join(firmwareDir, "manifest.json.tmp")
	if err := os.WriteFile(tmpMf, b, 0o644); err != nil {
		r.Response.WriteStatusExit(http.StatusInternalServerError, g.Map{"error": err.Error()})
		return
	}
	_ = os.Rename(tmpMf, filepath.Join(firmwareDir, "manifest.json"))

	g.Log().Infof(r.Context(), "firmware pushed: version=%s md5=%s size=%d", version, md5hex, len(data))
	r.Response.WriteJson(g.Map{"ok": true, "version": version, "md5": md5hex, "size": len(data)})
}

// ---------- 工具 ----------

func gconvString(v interface{}) string {
	if v == nil {
		return ""
	}
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}

// versionLess 报告 a 是否严格小于 b（按 X.Y.Z 逐段整数比较）
func versionLess(a, b string) bool {
	pa := strings.Split(a, ".")
	pb := strings.Split(b, ".")
	for i := 0; i < len(pa) && i < len(pb); i++ {
		ia, _ := strconv.Atoi(pa[i])
		ib, _ := strconv.Atoi(pb[i])
		if ia != ib {
			return ia < ib
		}
	}
	return len(pa) < len(pb)
}
