package main

import (
	"bufio"
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
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
	upstreamASR = envDefault("UPSTREAM_ASR", "asr:10300")
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

// ---------- /transcribe：将设备 16kHz/16bit/单声道 PCM 转发到 Wyoming Faster-Whisper ----------

type wyomingEvent struct {
	Type          string          `json:"type"`
	Data          json.RawMessage `json:"data,omitempty"`
	DataLength    int             `json:"data_length,omitempty"`
	PayloadLength int             `json:"payload_length,omitempty"`
}

type wyomingTranscript struct {
	Text string `json:"text"`
}

func transcribeHandler(r *ghttp.Request) {
	pcm := r.GetBody()
	if len(pcm) == 0 {
		r.Response.WriteStatusExit(http.StatusBadRequest, g.Map{"error": "empty body"})
		return
	}

	text, err := transcribeWyoming(r.Context(), pcm)
	if err != nil {
		g.Log().Warningf(r.Context(), "Wyoming transcribe failed: %v", err)
		r.Response.WriteStatusExit(http.StatusBadGateway, g.Map{"error": "asr unavailable"})
		return
	}
	r.Response.WriteJson(g.Map{"text": text})
}

func transcribeWyoming(ctx context.Context, pcm []byte) (string, error) {
	dialer := net.Dialer{Timeout: 5 * time.Second}
	conn, err := dialer.DialContext(ctx, "tcp", upstreamASR)
	if err != nil {
		return "", fmt.Errorf("dial %s: %w", upstreamASR, err)
	}
	defer conn.Close()
	if deadline, ok := ctx.Deadline(); ok {
		_ = conn.SetDeadline(deadline)
	} else {
		_ = conn.SetDeadline(time.Now().Add(45 * time.Second))
	}

	writer := bufio.NewWriter(conn)
	// Wyoming ASR 流顺序：transcribe -> audio-start -> 若干 audio-chunk -> audio-stop。
	if err := writeWyomingEvent(writer, wyomingEvent{Type: "transcribe", Data: json.RawMessage(`{"language":"zh"}`)}, nil); err != nil {
		return "", err
	}
	format := json.RawMessage(`{"rate":16000,"width":2,"channels":1}`)
	if err := writeWyomingEvent(writer, wyomingEvent{Type: "audio-start", Data: format}, nil); err != nil {
		return "", err
	}
	const chunkSize = 3200 // 100 ms PCM：16000 Hz * 2 bytes
	for start := 0; start < len(pcm); start += chunkSize {
		end := start + chunkSize
		if end > len(pcm) {
			end = len(pcm)
		}
		if err := writeWyomingEvent(writer, wyomingEvent{Type: "audio-chunk", Data: format}, pcm[start:end]); err != nil {
			return "", err
		}
	}
	if err := writeWyomingEvent(writer, wyomingEvent{Type: "audio-stop"}, nil); err != nil {
		return "", err
	}
	if err := writer.Flush(); err != nil {
		return "", err
	}

	reader := bufio.NewReader(conn)
	for {
		event, err := readWyomingEvent(reader)
		if err != nil {
			return "", err
		}
		switch event.Type {
		case "transcript":
			var transcript wyomingTranscript
			if err := json.Unmarshal(event.Data, &transcript); err != nil {
				return "", fmt.Errorf("decode transcript: %w", err)
			}
			return transcript.Text, nil
		case "error":
			return "", fmt.Errorf("upstream error: %s", string(event.Data))
		}
	}
}

func writeWyomingEvent(w *bufio.Writer, event wyomingEvent, payload []byte) error {
	event.PayloadLength = len(payload)
	header, err := json.Marshal(event)
	if err != nil {
		return err
	}
	if _, err = w.Write(header); err != nil {
		return err
	}
	if err = w.WriteByte('\n'); err != nil {
		return err
	}
	if len(payload) > 0 {
		_, err = w.Write(payload)
	}
	return err
}

func readWyomingEvent(r *bufio.Reader) (wyomingEvent, error) {
	line, err := r.ReadBytes('\n')
	if err != nil {
		return wyomingEvent{}, err
	}
	var event wyomingEvent
	if err = json.Unmarshal(line, &event); err != nil {
		return wyomingEvent{}, fmt.Errorf("decode Wyoming header: %w", err)
	}
	if event.DataLength < 0 || event.DataLength > 64*1024 || event.PayloadLength < 0 || event.PayloadLength > 1024*1024 {
		return wyomingEvent{}, fmt.Errorf("invalid Wyoming frame lengths data=%d payload=%d", event.DataLength, event.PayloadLength)
	}
	// Python Wyoming 参考实现将 data 作为 header 之后的独立 JSON 段发送。
	if event.DataLength > 0 {
		event.Data = make([]byte, event.DataLength)
		if _, err = io.ReadFull(r, event.Data); err != nil {
			return wyomingEvent{}, err
		}
	}
	if event.PayloadLength > 0 {
		if _, err = io.CopyN(io.Discard, r, int64(event.PayloadLength)); err != nil {
			return wyomingEvent{}, err
		}
	}
	return event, nil
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
