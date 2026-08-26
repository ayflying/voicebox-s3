# 多阶段构建：golang:1.22-alpine 编译，alpine 运行
FROM golang:1.22-alpine AS build
WORKDIR /src
# 先拉依赖（利用层缓存）
COPY go.mod ./
RUN go mod download || true
COPY . .
# 生成 go.sum 并构建静态二进制
RUN go mod tidy && CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o /out/gateway .

FROM alpine:3.20
RUN apk add --no-cache ca-certificates
WORKDIR /app
COPY --from=build /out/gateway /app/gateway
EXPOSE 18080
ENTRYPOINT ["/app/gateway"]
