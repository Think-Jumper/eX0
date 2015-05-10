package main

import (
	"encoding/binary"
	"errors"
	"fmt"

	"github.com/shurcooL/go/gists/gist6545684"
	glfw "github.com/shurcooL/goglfw"
	"golang.org/x/mobile/f32"
	"golang.org/x/mobile/gl"
	"golang.org/x/mobile/gl/glutil"
)

func newLevel(name string) (*level, error) {
	l := new(level)

	err := l.initShaders()
	if err != nil {
		return nil, err
	}
	{
		f, err := glfw.Open(name)
		if err != nil {
			return nil, err
		}
		l.polygon, err = gist6545684.ReadGpcFromReader(f)
		f.Close()
		if err != nil {
			return nil, err
		}
	}
	err = l.createVbo()
	if err != nil {
		return nil, err
	}

	return l, nil
}

type level struct {
	polygon gist6545684.Polygon

	program                 gl.Program
	pMatrixUniform          gl.Uniform
	mvMatrixUniform         gl.Uniform
	vertexPositionBuffer    gl.Buffer
	vertexPositionAttribute gl.Attrib
}

const (
	levelVertexSource = `//#version 120 // OpenGL 2.1.
//#version 100 // WebGL.

attribute vec3 aVertexPosition;

uniform mat4 uMVMatrix;
uniform mat4 uPMatrix;

void main() {
	gl_Position = uPMatrix * uMVMatrix * vec4(aVertexPosition, 1.0);
}
`
	levelFragmentSource = `//#version 120 // OpenGL 2.1.
//#version 100 // WebGL.

void main() {
	gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
`
)

func (l *level) initShaders() error {
	var err error
	l.program, err = glutil.CreateProgram(characterVertexSource, characterFragmentSource)
	if err != nil {
		return err
	}

	gl.ValidateProgram(l.program)
	if gl.GetProgrami(l.program, gl.VALIDATE_STATUS) != gl.TRUE {
		return errors.New("VALIDATE_STATUS: " + gl.GetProgramInfoLog(l.program))
	}

	gl.UseProgram(l.program)

	l.pMatrixUniform = gl.GetUniformLocation(l.program, "uPMatrix")
	l.mvMatrixUniform = gl.GetUniformLocation(l.program, "uMVMatrix")

	if glError := gl.GetError(); glError != 0 {
		return fmt.Errorf("gl.GetError: %v", glError)
	}

	return nil
}

func (l *level) createVbo() error {
	l.vertexPositionBuffer = gl.CreateBuffer()
	gl.BindBuffer(gl.ARRAY_BUFFER, l.vertexPositionBuffer)
	var vertices []float32
	for _, contour := range l.polygon.Contours {
		for _, vertex := range contour.Vertices {
			vertices = append(vertices, float32(vertex[0]), float32(vertex[1]))
		}
	}
	gl.BufferData(gl.ARRAY_BUFFER, f32.Bytes(binary.LittleEndian, vertices...), gl.STATIC_DRAW)

	l.vertexPositionAttribute = gl.GetAttribLocation(l.program, "aVertexPosition")
	gl.EnableVertexAttribArray(l.vertexPositionAttribute)

	if glError := gl.GetError(); glError != 0 {
		return fmt.Errorf("gl.GetError: %v", glError)
	}

	return nil
}

func (l *level) setup() {
	gl.UseProgram(l.program)
	gl.BindBuffer(gl.ARRAY_BUFFER, l.vertexPositionBuffer)

	gl.VertexAttribPointer(l.vertexPositionAttribute, 2, gl.FLOAT, false, 0, 0)
}

func (l *level) render() {
	var first int
	for _, contour := range l.polygon.Contours {
		count := len(contour.Vertices)
		gl.DrawArrays(gl.LINE_LOOP, first, count)
		first += count
	}
}
