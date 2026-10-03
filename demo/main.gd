extends Node3D

const FPS_STEPS: Array[int] = [0, 30, 60]
const SIZES: Array[Vector2i] = [Vector2i(1280, 720), Vector2i(1920, 1080)]
const ORBIT_RADIUS := 7.0
const ORBIT_HEIGHT := 3.0
const ORBIT_SPEED := 0.4
const LOOK_TARGET := Vector3(0, 0.75, 0)

@onready var exporter: SpectatorStreamExporter = $SpectatorStreamExporter
@onready var spectator_viewport: SubViewport = $SpectatorViewport
@onready var spectator_camera: Camera3D = $SpectatorViewport/SpectatorCamera
@onready var player_camera: Camera3D = $PlayerCamera
@onready var props: Node3D = $Props
@onready var preview: TextureRect = $UI/Preview
@onready var status_label: Label = $UI/Status

var _orbit_angle := 0.0
var _size_index := 0
var _last_error := ""


func _ready() -> void:
	player_camera.look_at_from_position(Vector3(0, 4, 9), LOOK_TARGET)
	spectator_viewport.size = SIZES[_size_index]
	preview.texture = spectator_viewport.get_texture()

	exporter.stream_started.connect(_on_stream_started)
	exporter.stream_stopped.connect(_update_status)
	exporter.stream_error.connect(_on_stream_error)

	if RenderingServer.get_current_rendering_method() == "gl_compatibility":
		_last_error = "Compatibility renderer: no frames will be sent. Use Forward+ or Mobile."

	exporter.active = true
	_update_status()


func _process(delta: float) -> void:
	props.rotate_y(delta * 0.6)

	_orbit_angle = wrapf(_orbit_angle + delta * ORBIT_SPEED, 0.0, TAU)
	var orbit_pos := Vector3(cos(_orbit_angle) * ORBIT_RADIUS, ORBIT_HEIGHT, sin(_orbit_angle) * ORBIT_RADIUS)
	spectator_camera.look_at_from_position(orbit_pos, LOOK_TARGET)


func _unhandled_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return

	match key.keycode:
		KEY_SPACE:
			_last_error = ""
			exporter.active = not exporter.active
		KEY_F:
			var next := (FPS_STEPS.find(exporter.target_fps) + 1) % FPS_STEPS.size()
			exporter.target_fps = FPS_STEPS[next]
		KEY_V:
			_size_index = (_size_index + 1) % SIZES.size()
			spectator_viewport.size = SIZES[_size_index]
			exporter.restart()
		KEY_R:
			_last_error = ""
			exporter.restart()
		_:
			return

	_update_status()


func _on_stream_started() -> void:
	_last_error = ""
	_update_status()


func _on_stream_error(message: String) -> void:
	_last_error = message
	_update_status()


func _update_status() -> void:
	var size := spectator_viewport.size
	var fps_text := "unlimited" if exporter.target_fps == 0 else str(exporter.target_fps)
	var lines: PackedStringArray = [
		"Stream:     %s" % ("LIVE" if exporter.active else "stopped"),
		"Name:       %s" % exporter.stream_name,
		"Size:       %dx%d" % [size.x, size.y],
		"Target FPS: %s" % fps_text,
		"Renderer:   %s" % RenderingServer.get_current_rendering_method(),
	]
	if not _last_error.is_empty():
		lines.append("Error:      %s" % _last_error)
	lines.append("")
	lines.append("[Space] toggle   [F] fps   [V] resolution   [R] restart")
	status_label.text = "\n".join(lines)
